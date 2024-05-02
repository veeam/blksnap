// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Veeam Software Group GmbH */
#define pr_fmt(fmt) KBUILD_MODNAME "-diff-area: " fmt

#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/build_bug.h>
#ifdef BLKSNAP_STANDALONE
#include "veeamblksnap.h"
#include "compat.h"
#else
#include <uapi/linux/blksnap.h>
#endif
#include "chunk.h"
#include "diff_buffer.h"
#include "diff_storage.h"
#include "params.h"
#include "tracker.h"
#ifdef BLKSNAP_FILELOG
#include "log.h"
#endif
#ifdef BLKSNAP_MEMSTAT
#include "memstat.h"
#endif
#ifdef BLKSNAP_HISTOGRAM
#include "log_histogram.h"
#endif
struct cow_task {
	struct list_head link;
	struct bio *bio;
};

static inline sector_t diff_area_chunk_offset(struct diff_area *diff_area,
					      sector_t sector)
{
	return sector & ((1ull << (diff_area->chunk_shift - SECTOR_SHIFT)) - 1);
}

static inline unsigned long diff_area_chunk_number(struct diff_area *diff_area,
						   sector_t sector)
{
	return (unsigned long)(sector >>
			       (diff_area->chunk_shift - SECTOR_SHIFT));
}

static inline sector_t chunk_sector(struct chunk *chunk)
{
	return (sector_t)(chunk->number)
	       << (chunk->diff_area->chunk_shift - SECTOR_SHIFT);
}

static inline sector_t last_chunk_size(sector_t sector_count, sector_t capacity)
{
	sector_t capacity_rounded = round_down(capacity, sector_count);

	if (capacity > capacity_rounded)
		sector_count = capacity - capacity_rounded;

	return sector_count;
}

static inline unsigned long long count_by_shift(sector_t capacity,
						unsigned long long shift)
{
	unsigned long long shift_sector = (shift - SECTOR_SHIFT);

	return round_up(capacity, (1ull << shift_sector)) >> shift_sector;
}

static inline struct chunk *chunk_alloc(struct diff_area *diff_area,
					unsigned long number)
{
	struct chunk *chunk;

#ifdef BLKSNAP_MEMSTAT
	chunk = __kzalloc(sizeof(struct chunk), GFP_KERNEL);
#else
	chunk = kzalloc(sizeof(struct chunk), GFP_KERNEL);
#endif
	if (!chunk)
		return NULL;

	INIT_LIST_HEAD(&chunk->link);
	sema_init(&chunk->lock, 1);
	chunk->diff_area = NULL;
	chunk->number = number;
	chunk->state = CHUNK_ST_NEW;

	chunk->sector_count = diff_area_chunk_sectors(diff_area);
	/*
	 * The last chunk has a special size.
	 */
	if (unlikely((number + 1) == diff_area->chunk_count))
		chunk->sector_count = bdev_nr_sectors(diff_area->orig_bdev) -
						(chunk->sector_count * number);
#ifdef CONFIG_BLKSNAP_CHUNK_DBG
	chunk->holder_func = NULL;
	chunk->holder_sector = 0;
#endif
	return chunk;
}

static inline void chunk_free(struct diff_area *diff_area, struct chunk *chunk)
{
	down(&chunk->lock);
	if (chunk->diff_buffer)
		diff_buffer_release(diff_area, chunk->diff_buffer);
	up(&chunk->lock);
#ifdef BLKSNAP_MEMSTAT
	__kfree(chunk);
#else
	kfree(chunk);
#endif
}

static void diff_area_calculate_chunk_size(struct diff_area *diff_area)
{
	unsigned long count;
	unsigned long shift = get_chunk_minimum_shift();
	sector_t capacity;
	sector_t min_io_sect;

	min_io_sect = (sector_t)(bdev_io_min(diff_area->orig_bdev) >>
								SECTOR_SHIFT);
	capacity = bdev_nr_sectors(diff_area->orig_bdev);
	pr_debug("Minimal IO block %llu sectors\n", min_io_sect);
	pr_debug("Device capacity %llu sectors\n", capacity);

	count = count_by_shift(capacity, shift);
	pr_debug("Chunks count %lu\n", count);
	while ((count > get_chunk_maximum_count()) ||
		((1ul << (shift - SECTOR_SHIFT)) < min_io_sect)) {
		shift++;
		count = count_by_shift(capacity, shift);
		pr_debug("Chunks count %lu\n", count);
	}

	diff_area->chunk_shift = shift;
	diff_area->chunk_count = (unsigned long)DIV_ROUND_UP_ULL(capacity,
					(1ul << (shift - SECTOR_SHIFT)));
}

void diff_area_free(struct kref *kref)
{
	unsigned long inx = 0;
	struct chunk *chunk;
	struct diff_area *diff_area;

	might_sleep();
	diff_area = container_of(kref, struct diff_area, kref);

	xa_for_each(&diff_area->chunk_map, inx, chunk) {
		if (chunk)
			chunk_free(diff_area, chunk);
	}
	xa_destroy(&diff_area->chunk_map);

#ifdef BLKSNAP_STANDALONE
	pr_debug("Difference area statistic for device [%d:%d]\n",
		MAJOR(diff_area->orig_bdev->bd_dev),
		MINOR(diff_area->orig_bdev->bd_dev));
	pr_debug("%llu MiB was processed\n", atomic64_read(&diff_area->stat_processed) >> (20 - SECTOR_SHIFT));
	pr_debug("%llu MiB was copied\n", atomic64_read(&diff_area->stat_copied) >> (20 - SECTOR_SHIFT));
	pr_debug("%llu MiB was read from image\n", atomic64_read(&diff_area->stat_image_read) >> (20 - SECTOR_SHIFT));
	pr_debug("%llu MiB was written to image\n", atomic64_read(&diff_area->stat_image_written) >> (20 - SECTOR_SHIFT));
#endif
#ifdef BLKSNAP_HISTOGRAM
	pr_debug("Image IO units statistic:\n");
	log_histogram_show(&diff_area->image_hg);
	pr_debug("Copy-on-write IO units statistic:\n");
	log_histogram_show(&diff_area->cow_hg);
#endif

	diff_buffer_cleanup(diff_area);
	tracker_put(diff_area->tracker);
#ifdef BLKSNAP_MEMSTAT
	__kfree(diff_area);
#else
	kfree(diff_area);
#endif
}

static inline bool diff_area_store_one(struct diff_area *diff_area)
{
	struct chunk *iter, *chunk = NULL;

	spin_lock(&diff_area->store_queue_lock);
	list_for_each_entry(iter, &diff_area->store_queue, link) {
		if (!down_trylock(&iter->lock)) {
			chunk = iter;
			atomic_dec(&diff_area->store_queue_count);
			list_del_init(&chunk->link);
			chunk->diff_area = diff_area_get(diff_area);
			break;
		}
		/*
		 * If it is not possible to lock a chunk for writing, then it is
		 * currently in use, and we try to clean up the next chunk.
		 */
	}
	if (!chunk)
		diff_area->store_queue_processing = false;
	spin_unlock(&diff_area->store_queue_lock);
	if (!chunk)
		return false;

	if (chunk->state != CHUNK_ST_IN_MEMORY) {
		/*
		 * There cannot be a chunk in the store queue whose buffer has
		 * not been read into memory.
		 */
		chunk_up(chunk);
		pr_warn("Cannot release empty buffer for chunk #%ld",
			chunk->number);
		return true;
	}

	if (diff_area_is_corrupted(diff_area)) {
		chunk_store_failed(chunk, 0);
		return true;
	}
	if (!chunk->diff_file && !chunk->diff_bdev) {
		int ret;

		ret = diff_storage_alloc(diff_area->diff_storage,
					 diff_area_chunk_sectors(diff_area),
					 &chunk->diff_bdev,
					 &chunk->diff_file,
					 &chunk->diff_ofs_sect);
		if (ret) {
			pr_debug("Cannot get store for chunk #%ld\n",
				 chunk->number);
			chunk_store_failed(chunk, ret);
			return true;
		}
	}
	if (chunk->diff_bdev) {
		chunk_store_tobdev(chunk);
		return true;
	}
	chunk_diff_write(chunk);
	return true;
}
#ifdef CONFIG_BLKSNAP_COW_SCHEDULE
static int diff_area_cow_schedule(struct diff_area *diff_area, struct bio *bio)
{
	struct cow_task *task;

#ifdef BLKSNAP_MEMSTAT
	task = __kzalloc(sizeof(struct cow_task), GFP_KERNEL);
#else
	task = kzalloc(sizeof(struct cow_task), GFP_KERNEL);
#endif
	if (!task)
		return -ENOMEM;

	INIT_LIST_HEAD(&task->link);
	task->bio = bio;
	bio_get(bio);

	spin_lock(&diff_area->cow_queue_lock);
	list_add_tail(&task->link, &diff_area->cow_queue);
	spin_unlock(&diff_area->cow_queue_lock);

	blksnap_queue_work(&diff_area->cow_queue_work);
	return 0;
}

static inline struct bio *diff_area_cow_get_bio(struct diff_area *diff_area)
{
	struct bio *bio = NULL;
	struct cow_task *task;

	spin_lock(&diff_area->cow_queue_lock);
	task = list_first_entry_or_null(&diff_area->cow_queue,
							struct cow_task, link);
	if (task)
		list_del(&task->link);
	spin_unlock(&diff_area->cow_queue_lock);

	if (task) {
		bio = task->bio;
#ifdef BLKSNAP_MEMSTAT
		__kfree(task);
#else
		kfree(task);
#endif
	}
	return bio;
}

static void diff_area_cow_queue_work(struct work_struct *work)
{
	struct diff_area *diff_area = container_of(work, struct diff_area,
							cow_queue_work);
	struct bio *bio;

	while ((bio = diff_area_cow_get_bio(diff_area))) {
		if (!diff_area_cow_process_bio(diff_area, bio)) {
#ifdef BLKSNAP_STANDALONE
			submit_bio_noacct_notrace(bio);
#else
			resubmit_filtered_bio(bio);
#endif
		}
		bio_put(bio);
	}
}
#endif
static void diff_area_store_queue_work(struct work_struct *work)
{
	struct diff_area *diff_area = container_of(
		work, struct diff_area, store_queue_work);
	unsigned int old_nofs;
#if !defined(BLKSNAP_STANDALONE)
	struct blkfilter *prev_filter = current->blk_filter;

	current->blk_filter = &diff_area->tracker->filter;
#endif
	old_nofs = memalloc_nofs_save();
	while (diff_area_store_one(diff_area))
		;
	memalloc_nofs_restore(old_nofs);
#if !defined(BLKSNAP_STANDALONE)
	current->blk_filter = prev_filter;
#endif
}

static inline struct chunk_io_ctx *chunk_io_ctx_take(
						struct diff_area *diff_area)
{
	struct chunk_io_ctx *io_ctx;

	spin_lock(&diff_area->image_io_queue_lock);
	io_ctx = list_first_entry_or_null(&diff_area->image_io_queue,
						  struct chunk_io_ctx, link);
	if (io_ctx)
		list_del(&io_ctx->link);
	spin_unlock(&diff_area->image_io_queue_lock);

	return io_ctx;
}

static void diff_area_image_io_work(struct work_struct *work)
{
	struct diff_area *diff_area = container_of(
		work, struct diff_area, image_io_work);
	struct chunk_io_ctx *io_ctx;
	unsigned int old_nofs;
#if !defined(BLKSNAP_STANDALONE)
	struct blkfilter *prev_filter = current->blk_filter;

	current->blk_filter = &diff_area->tracker->filter;
#endif
	old_nofs = memalloc_nofs_save();
	while ((io_ctx = chunk_io_ctx_take(diff_area)))
		chunk_diff_bio_execute(io_ctx);
	memalloc_nofs_restore(old_nofs);
#if !defined(BLKSNAP_STANDALONE)
	current->blk_filter = prev_filter;
#endif
}

struct diff_area *diff_area_new(struct tracker *tracker,
				struct diff_storage *diff_storage)
{
	struct diff_area *diff_area = NULL;
	struct block_device *bdev = tracker->orig_bdev;

#ifdef BLKSNAP_MEMSTAT
	diff_area = __kzalloc(sizeof(struct diff_area), GFP_KERNEL);
#else
	diff_area = kzalloc(sizeof(struct diff_area), GFP_KERNEL);
#endif
	if (!diff_area)
		return ERR_PTR(-ENOMEM);

	kref_init(&diff_area->kref);
	diff_area->orig_bdev = bdev;
	diff_area->diff_storage = diff_storage;

	diff_area_calculate_chunk_size(diff_area);
	if (diff_area->chunk_shift > get_chunk_maximum_shift()) {
		pr_info("The maximum allowable chunk size has been reached.\n");
#ifdef BLKSNAP_MEMSTAT
		__kfree(diff_area);
#else
		kfree(diff_area);
#endif
		return ERR_PTR(-EFAULT);
	}
	pr_debug("The optimal chunk size was calculated as %llu bytes for device [%d:%d]\n",
		 (1ull << diff_area->chunk_shift),
		 MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev));

	xa_init(&diff_area->chunk_map);

	tracker_get(tracker);
	diff_area->tracker = tracker;
#ifdef CONFIG_BLKSNAP_COW_SCHEDULE
	spin_lock_init(&diff_area->cow_queue_lock);
	INIT_LIST_HEAD(&diff_area->cow_queue);
	INIT_WORK(&diff_area->cow_queue_work, diff_area_cow_queue_work);
#endif
	spin_lock_init(&diff_area->store_queue_lock);
	INIT_LIST_HEAD(&diff_area->store_queue);
	atomic_set(&diff_area->store_queue_count, 0);
	INIT_WORK(&diff_area->store_queue_work, diff_area_store_queue_work);

	spin_lock_init(&diff_area->free_diff_buffers_lock);
	INIT_LIST_HEAD(&diff_area->free_diff_buffers);
	atomic_set(&diff_area->free_diff_buffers_count, 0);

	spin_lock_init(&diff_area->image_io_queue_lock);
	INIT_LIST_HEAD(&diff_area->image_io_queue);
	INIT_WORK(&diff_area->image_io_work, diff_area_image_io_work);

	diff_area->physical_blksz = bdev_physical_block_size(bdev);
	diff_area->logical_blksz = bdev_logical_block_size(bdev);
	diff_area->corrupt_flag = 0;
	diff_area->store_queue_processing = false;

#ifdef BLKSNAP_STANDALONE
	atomic64_set(&diff_area->stat_processed, 0);
	atomic64_set(&diff_area->stat_copied, 0);
	atomic64_set(&diff_area->stat_processed, 0);
	atomic64_set(&diff_area->stat_image_written, 0);
#endif
#ifdef BLKSNAP_HISTOGRAM
	log_histogram_init(&diff_area->image_hg, 4096);
	log_histogram_init(&diff_area->cow_hg, 4096);
#endif

	return diff_area;
}

static inline unsigned int chunk_limit(struct chunk *chunk,
				       struct bvec_iter *iter)
{
	sector_t chunk_ofs = iter->bi_sector - chunk_sector(chunk);
	sector_t chunk_left = chunk->sector_count - chunk_ofs;

	return min(iter->bi_size, (unsigned int)(chunk_left << SECTOR_SHIFT));
}

/*
 * Implements the copy-on-write mechanism.
 */
bool diff_area_cow_process_bio(struct diff_area *diff_area, struct bio *bio)
{
	bool skip_bio = false;
	bool nowait = bio->bi_opf & REQ_NOWAIT;
	struct bvec_iter iter = bio->bi_iter;
	struct bio *chunk_bio = NULL;
	LIST_HEAD(chunks);
	int ret = 0;
	unsigned int flags;

#if !defined(BLKSNAP_STANDALONE)
	if (bio_flagged(bio, BIO_REMAPPED))
		iter.bi_sector -= bio->bi_bdev->bd_start_sect;
#endif

#ifdef BLKSNAP_STANDALONE
	atomic64_add(iter.bi_size >> SECTOR_SHIFT, &diff_area->stat_processed);
#endif
	flags = memalloc_noio_save();
	while (iter.bi_size) {
		unsigned long nr = diff_area_chunk_number(diff_area,
								iter.bi_sector);
		struct chunk *chunk = xa_load(&diff_area->chunk_map, nr);
		unsigned int len;

		if (!chunk) {
			chunk = chunk_alloc(diff_area, nr);
			if (!chunk) {
				diff_area_set_corrupted(diff_area, -EINVAL);
				ret = -ENOMEM;
				goto fail;
			}

			ret = xa_insert(&diff_area->chunk_map, nr, chunk,
					GFP_KERNEL);
			if (likely(!ret)) {
				/* new chunk has been added */
			} else if (ret == -EBUSY) {
				/* another chunk has just been created */
				chunk_free(diff_area, chunk);
				chunk = xa_load(&diff_area->chunk_map, nr);
				WARN_ON_ONCE(!chunk);
				if (unlikely(!chunk)) {
					ret = -EINVAL;
					diff_area_set_corrupted(diff_area, ret);
					goto fail;
				}
			} else if (ret) {
				pr_err("Failed insert chunk to chunk map\n");
				chunk_free(diff_area, chunk);
				diff_area_set_corrupted(diff_area, ret);
				goto fail;
			}
		}

		if (nowait) {
			if (down_trylock(&chunk->lock)) {
				ret = -EAGAIN;
				goto fail;
			}
		} else {
#ifdef CONFIG_BLKSNAP_CHUNK_DBG
			ret = down_timeout(&chunk->lock, HZ * 15);
			if (unlikely(ret)) {
				if (ret == -ETIME) {
					pr_err("%s: Hang on chunk #%lu\n", __func__, nr);
					pr_err("state %d\n", chunk->state);
					if (chunk->holder_func) {
						pr_err("holder %s\n", chunk->holder_func);
						pr_err("holder sector: %llu\n", chunk->holder_sector);
						pr_err("current sector: %llu\n", iter.bi_sector);
					}
				}
				goto fail;
			}
			chunk->holder_func = __func__;
			chunk->holder_sector = iter.bi_sector;
#else
			ret = down_killable(&chunk->lock);
			if (unlikely(ret))
				goto fail;
#endif
		}
		chunk->diff_area = diff_area_get(diff_area);

		len = chunk_limit(chunk, &iter);
#ifdef HAVE_BIO_ADVANCE_ITER_SIMPLE
		bio_advance_iter_single(bio, &iter, len);
#else
		bio_advance_iter(bio, &iter, len);
#endif

		if (chunk->state == CHUNK_ST_NEW) {
			if (nowait) {
				/*
				 * If the data of this chunk has not yet been
				 * copied to the difference storage, then it is
				 * impossible to process the I/O write unit with
				 * the NOWAIT flag.
				 */
				chunk_up(chunk);
				ret = -EAGAIN;
				goto fail;
			}

#ifdef BLKSNAP_STANDALONE
			atomic64_add(chunk->sector_count, &diff_area->stat_copied);
#endif
			/*
			 * Load the chunk asynchronously.
			 */
			ret = chunk_load_and_postpone_io(chunk, &chunk_bio);
			if (ret) {
				chunk_up(chunk);
				goto fail;
			}
			list_add_tail(&chunk->link, &chunks);
		} else {
			/*
			 * The chunk has already been:
			 *   - failed, when the snapshot is corrupted
			 *   - read into the buffer
			 *   - stored into the diff storage
			 * In this case, we do not change the chunk.
			 */
			chunk_up(chunk);
		}
	}

	if (chunk_bio) {
		/* Postpone bio processing in a callback. */
#ifdef BLKSNAP_HISTOGRAM
		log_histogram_add(&diff_area->cow_hg, chunk_bio->bi_iter.bi_size);
#endif
		chunk_load_and_postpone_io_finish(&chunks, chunk_bio, bio);
		skip_bio = true;

	}
	/* Pass bio to the low level */
	goto out;

fail:
	if (chunk_bio) {
		chunk_bio->bi_status = errno_to_blk_status(ret);
		bio_endio(chunk_bio);
	}

	if (ret == -EAGAIN) {
		/*
		 * The -EAGAIN error code means that it is not possible to
		 * process a I/O unit with a flag REQ_NOWAIT.
		 * I/O unit processing is being completed with such error.
		 */
		bio->bi_status = BLK_STS_AGAIN;
		bio_endio(bio);
		skip_bio = true;
	} else
		diff_area_set_corrupted(diff_area, ret);
out:
	memalloc_noio_restore(flags);
	return skip_bio;
}

bool diff_area_cow(struct diff_area *diff_area, struct bio *bio)
{
#ifdef CONFIG_BLKSNAP_COW_SCHEDULE
	int ret;
#endif
	bool skip_bio = true;
	unsigned int flags;

	if (bio->bi_opf & REQ_NOWAIT) {
		bio->bi_status = BLK_STS_AGAIN;
		bio_endio(bio);
		return skip_bio;
	}

	flags = memalloc_noio_save();
#ifdef CONFIG_BLKSNAP_COW_SCHEDULE
	ret = diff_area_cow_schedule(diff_area, bio);
	if (ret) {
		diff_area_set_corrupted(diff_area, ret);
		skip_bio = false;
	}
#else
	skip_bio  = diff_area_cow_process_bio(diff_area, bio);
#endif
	memalloc_noio_restore(flags);

	return skip_bio;
}

static void orig_clone_bio(struct diff_area *diff_area, struct bio *bio)
{
	struct bio *new_bio;
	struct block_device *bdev = diff_area->orig_bdev;
	sector_t chunk_limit;

	new_bio = chunk_alloc_clone(bdev, bio);
	WARN_ON(!new_bio);

	chunk_limit = diff_area_chunk_sectors(diff_area) -
		      diff_area_chunk_offset(diff_area, bio->bi_iter.bi_sector);

	new_bio->bi_iter.bi_sector = bio->bi_iter.bi_sector;
	new_bio->bi_iter.bi_size = min_t(unsigned int,
			bio->bi_iter.bi_size, chunk_limit << SECTOR_SHIFT);

	bio_advance(bio, new_bio->bi_iter.bi_size);
	bio_chain(new_bio, bio);
#ifdef BLKSNAP_HISTOGRAM
	log_histogram_add(&diff_area->image_hg, new_bio->bi_iter.bi_size);
#endif
#ifdef BLKSNAP_STANDALONE
	submit_bio_noacct_notrace(new_bio);
#else
	submit_bio_noacct(new_bio);
#endif
}

bool diff_area_submit_chunk(struct diff_area *diff_area, struct bio *bio)
{
	int ret;
	unsigned long nr;
	struct chunk *chunk;

	nr = diff_area_chunk_number(diff_area, bio->bi_iter.bi_sector);
	chunk = xa_load(&diff_area->chunk_map, nr);
	/*
	 * If this chunk is not in the chunk map, then the COW algorithm did
	 * not access this part of the disk space, and writing to the snapshot
	 * in this part was also not performed.
	 */
	if (!chunk) {
		if (!op_is_write(bio_op(bio))) {
			/*
			 * To read, we simply redirect the bio to the original
			 * block device.
			 */
			orig_clone_bio(diff_area, bio);
			return true;
		}

		/*
		 * To process a write bio, we need to allocate a new chunk.
		 */
		chunk = chunk_alloc(diff_area, nr);
		WARN_ON_ONCE(!chunk);
		if (unlikely(!chunk))
			return false;

		ret = xa_insert(&diff_area->chunk_map, nr, chunk,GFP_KERNEL);
		if (likely(!ret)) {
			/* new chunk has been added */
		} else if (ret == -EBUSY) {
			/* another chunk has just been created */
			chunk_free(diff_area, chunk);
			chunk = xa_load(&diff_area->chunk_map, nr);
			WARN_ON_ONCE(!chunk);
			if (unlikely(!chunk))
				return false;
		} else if (ret) {
			pr_err("Failed insert chunk to chunk map\n");
			chunk_free(diff_area, chunk);
			return false;
		}
	}
#ifdef CONFIG_BLKSNAP_CHUNK_DBG
	ret = down_timeout(&chunk->lock, HZ * 15);
	if (unlikely(ret)) {
		if (ret == -ETIME) {
			pr_err("%s: Hang on chunk #%lu\n", __func__, nr);
			pr_err("state %d\n", chunk->state);
			if (chunk->holder_func) {
				pr_err("holder %s\n", chunk->holder_func);
				pr_err("holder sector: %llu\n", chunk->holder_sector);
				pr_err("current sector: %llu\n", bio->bi_iter.bi_sector);
			}
		}
		return false;
	}
	chunk->holder_func = __func__;
	chunk->holder_sector = bio->bi_iter.bi_sector;
#else
	if (down_killable(&chunk->lock))
		return false;
#endif
	chunk->diff_area = diff_area_get(diff_area);

	switch (chunk->state) {
	case CHUNK_ST_IN_MEMORY:
		/*
		 * Directly copy data from the in-memory chunk or
		 * copy to the in-memory chunk for write operation.
		 */
		chunk_copy_bio(chunk, bio, &bio->bi_iter);
		chunk_up(chunk);
		return true;
	case CHUNK_ST_STORED:
		/*
		 * Data is read from the difference storage or written to it.
		 */
		if (chunk->diff_bdev) {
			chunk_diff_bio_tobdev(chunk, bio);
			chunk_up(chunk);
			return true;
		}
		ret = chunk_diff_bio(chunk, bio);
		return (ret == 0);
	case CHUNK_ST_NEW:
		if (!op_is_write(bio_op(bio))) {
			/*
			 * Read from original block device
			 */
			orig_clone_bio(diff_area, bio);
			chunk_up(chunk);
			return true;
		}

		/*
		 * Starts asynchronous loading of a chunk from the original
		 * block device and schedule copying data to (or from) the
		 * in-memory chunk.
		 */
		return chunk_load_and_schedule_io(chunk, bio);
	default: /* CHUNK_ST_FAILED */
		pr_err("Chunk #%ld corrupted\n", chunk->number);
		chunk_up(chunk);
		return false;
	}
}

static inline void diff_area_event_corrupted(struct diff_area *diff_area)
{
	struct blksnap_event_corrupted data = {
		.dev_id_mj = MAJOR(diff_area->orig_bdev->bd_dev),
		.dev_id_mn = MINOR(diff_area->orig_bdev->bd_dev),
		.err_code = abs(diff_area->error_code),
	};

	event_gen(&diff_area->diff_storage->event_queue,
		  blksnap_event_code_corrupted,
		  &data,
		  sizeof(struct blksnap_event_corrupted));
}

void diff_area_set_corrupted(struct diff_area *diff_area, int err_code)
{
	if (test_and_set_bit(0, &diff_area->corrupt_flag))
		return;

	diff_area->error_code = err_code;
	diff_area_event_corrupted(diff_area);

	pr_err("Set snapshot device is corrupted for [%u:%u] with error code %d\n",
	       MAJOR(diff_area->orig_bdev->bd_dev),
	       MINOR(diff_area->orig_bdev->bd_dev), abs(err_code));
}
