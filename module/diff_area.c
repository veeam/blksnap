// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME "-diff-area: " fmt
#ifdef HAVE_GENHD_H
#include <linux/genhd.h>
#endif
#include <linux/blkdev.h>
#include <linux/slab.h>
#ifdef STANDALONE_BDEVFILTER
#include "blk_snap.h"
#else
#include <linux/blk_snap.h>
#endif
#include "memory_checker.h"
#include "params.h"
#include "chunk.h"
#include "diff_buffer.h"
#include "diff_storage.h"
#include "diff_io.h"
#ifdef STANDALONE_BDEVFILTER
#include "log.h"
#include "log_histogram.h"
#endif

#ifndef HAVE_BDEV_NR_SECTORS
static inline sector_t bdev_nr_sectors(struct block_device *bdev)
{
	return i_size_read(bdev->bd_inode) >> 9;
};
#endif

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
};

static inline sector_t chunk_sector(struct chunk *chunk)
{
	return (sector_t)(chunk->number)
	       << (chunk->diff_area->chunk_shift - SECTOR_SHIFT);
}

static inline void recalculate_last_chunk_size(struct chunk *chunk)
{
	sector_t capacity;

	capacity = bdev_nr_sectors(chunk->diff_area->orig_bdev);
	if (capacity > round_down(capacity, chunk->sector_count))
		chunk->sector_count =
			capacity - round_down(capacity, chunk->sector_count);
}

static inline unsigned long long count_by_shift(sector_t capacity,
						unsigned long long shift)
{
	unsigned long long shift_sector = (shift - SECTOR_SHIFT);

	return round_up(capacity, (1ull << shift_sector)) >> shift_sector;
}

static void diff_area_calculate_chunk_size(struct diff_area *diff_area)
{
	unsigned long long shift = chunk_minimum_shift;
	unsigned long long count;
	sector_t capacity;
	sector_t min_io_sect;

	min_io_sect = (sector_t)(bdev_io_min(diff_area->orig_bdev) >>
		SECTOR_SHIFT);
	capacity = bdev_nr_sectors(diff_area->orig_bdev);
	pr_debug("Minimal IO block %llu sectors\n", min_io_sect);
	pr_debug("Device capacity %llu sectors\n", capacity);

	count = count_by_shift(capacity, shift);
	pr_debug("Chunks count %llu\n", count);

	while ((count > get_chunk_maximum_count()) ||
		((1ull << (shift - SECTOR_SHIFT)) < min_io_sect)) {
		shift++;
		count = count_by_shift(capacity, shift);
		pr_debug("Chunks count %llu\n", count);
	}

	diff_area->chunk_shift = shift;
	diff_area->chunk_count = (unsigned long)DIV_ROUND_UP_ULL(capacity,
					(1ul << (shift - SECTOR_SHIFT)));
}

void diff_area_free(struct kref *kref)
{
	unsigned long inx = 0;
	struct chunk *chunk;
	struct diff_area *diff_area =
		container_of(kref, struct diff_area, kref);

	might_sleep();

	atomic_set(&diff_area->corrupt_flag, 1);
	flush_work(&diff_area->cache_release_work);
	xa_for_each(&diff_area->chunk_map, inx, chunk)
		if (chunk)
			chunk_free(diff_area, chunk);
	xa_destroy(&diff_area->chunk_map);

#ifdef STANDALONE_BDEVFILTER
	pr_debug("Difference area statistic for device [%d:%d]\n",
		MAJOR(diff_area->orig_bdev->bd_dev),
		MINOR(diff_area->orig_bdev->bd_dev));
	pr_debug("%llu MiB was processed\n", atomic64_read(&diff_area->stat_processed) >> (20 - SECTOR_SHIFT));
	pr_debug("%llu MiB was copied\n", atomic64_read(&diff_area->stat_copied) >> (20 - SECTOR_SHIFT));
	pr_debug("%llu MiB was read from image\n", atomic64_read(&diff_area->stat_image_read) >> (20 - SECTOR_SHIFT));
	pr_debug("%llu MiB was written to image\n", atomic64_read(&diff_area->stat_image_written) >> (20 - SECTOR_SHIFT));

	pr_debug("Reading IO units statistic:\n");
	log_histogram_show(&diff_area->read_hg);

	pr_debug("Redirection IO units statistic:\n");
	log_histogram_show(&diff_area->redirect_hg);
#endif

	if (diff_area->orig_bdev) {
#if defined(HAVE_BDEV_FILE_OPEN)
		bdev_fput(diff_area->orig_bdev_file);
#elif defined(HAVE_BDEV_HANDLE)
		bdev_release(diff_area->orig_bdev_handler);
		diff_area->orig_bdev_handler = NULL;
#elif defined(HAVE_BLK_HOLDER_OPS)
		blkdev_put(diff_area->orig_bdev, NULL);
#else
		blkdev_put(diff_area->orig_bdev, FMODE_READ | FMODE_WRITE);
#endif
		diff_area->orig_bdev = NULL;
	}

	/* Clean up free_diff_buffers */
	diff_buffer_cleanup(diff_area);

	kfree(diff_area);
	memory_object_dec(memory_object_diff_area);
}

static inline struct chunk *
get_chunk_from_cache_and_write_lock(struct diff_area *diff_area, int wr)
{

	struct list_head *cache_queue;
	atomic_t *cache_count;
	struct chunk *iter;
	struct chunk *chunk = NULL;
#ifdef BLK_SNAP_DEBUG_DIFF_BUFFER
	int locked_mutex_counter = 0;
#endif

	if (wr) {
		cache_queue = &diff_area->write_cache_queue;
		cache_count = &diff_area->write_cache_count;
	} else {
		cache_queue = &diff_area->read_cache_queue;
		cache_count = &diff_area->read_cache_count;
	}

	spin_lock(&diff_area->chunk_map_lock);
	list_for_each_entry(iter, cache_queue, link) {
		if (!down_trylock(&iter->lock)) {
			chunk = iter;
			break;
		}
		/*
		 * If it is not possible to lock a chunk for writing,
		 * then it is currently in use, and we try to clean up the
		 * next chunk.
		 */
#ifdef BLK_SNAP_DEBUG_DIFF_BUFFER
		locked_mutex_counter++;
#endif
	}
	if (likely(chunk)) {
		atomic_dec(cache_count);
		list_del_init(&chunk->link);
		chunk->diff_area = diff_area_get(diff_area);
		atomic_inc(&chunk->refcount);
	}
	spin_unlock(&diff_area->chunk_map_lock);

#ifdef BLK_SNAP_DEBUG_DIFF_BUFFER
	if (locked_mutex_counter)
		pr_debug("Found %d locked chunk\n", locked_mutex_counter);
#endif
	return chunk;
}

static struct chunk *
diff_area_get_chunk_from_cache_and_write_lock(struct diff_area *diff_area)
{
	if (atomic_read(&diff_area->read_cache_count) >
	    chunk_maximum_in_cache) {
		struct chunk *chunk = get_chunk_from_cache_and_write_lock(
			diff_area, READ);
		if (chunk) {
#ifdef BLK_SNAP_DEBUG_IMAGE_WRITE
			if (chunk_state_check(chunk, CHUNK_ST_DIRTY))
				pr_err("get and lock dirty chunk #%ld from read cache\n",
				       chunk->number);
#endif
			return chunk;
		}
	}

	if (atomic_read(&diff_area->write_cache_count) >
	    chunk_maximum_in_cache) {
		struct chunk *chunk = get_chunk_from_cache_and_write_lock(
			diff_area, WRITE);
		if (chunk) {
#ifdef BLK_SNAP_DEBUG_IMAGE_WRITE
			if (chunk_state_check(chunk, CHUNK_ST_DIRTY))
				pr_debug(
					"get and lock dirty chunk #%ld from write cache\n",
					chunk->number);
			else
				pr_err("get and lock pure chunk #%ld from write cache\n",
				       chunk->number);
#endif
			return chunk;
		}
	}

	return NULL;
}

static void diff_area_cache_release(struct diff_area *diff_area)
{
	struct chunk *chunk;

	while (!diff_area_is_corrupted(diff_area) &&
	       (chunk = diff_area_get_chunk_from_cache_and_write_lock(
			diff_area))) {
		/*
		 * There cannot be a chunk in the cache whose buffer is
		 * not ready.
		 */
		if (WARN(!chunk_state_check(chunk, CHUNK_ST_BUFFER_READY),
			 "Cannot release empty buffer for chunk #%ld",
			 chunk->number)) {
			chunk_up(chunk);
			continue;
		}

		if (chunk_state_check(chunk, CHUNK_ST_DIRTY)) {
			int ret;

#ifdef BLK_SNAP_DEBUG_IMAGE_WRITE
			pr_debug("Storing chunk #%ld\n", chunk->number);
#endif
			ret = chunk_schedule_storing(chunk, false);
			if (ret) {
#ifdef BLK_SNAP_DEBUG_IMAGE_WRITE
				pr_debug("Failed to store chunk #%ld\n",
					 chunk->number);
#endif
				chunk_store_failed(chunk, ret);
			}
		} else {
#ifdef BLK_SNAP_DEBUG_IMAGE_WRITE
			pr_debug("Release buffer for chunk #%ld\n",
				 chunk->number);
#endif
			chunk_diff_buffer_release(diff_area, chunk);
			if (chunk_state_check(chunk, CHUNK_ST_STORE_READY))
				chunk_up(chunk);
			else {
				/*
				 * A chunk is erased from the map if it does not
				 * store snapshot differences and does not
				 * contain snapshots changes.
				 * Such a chunk was created to read data from a
				 * snapshot image and does not require storage.
				 */
				chunk_up_and_free(chunk);
			}
		}
	}
}

static void diff_area_cache_release_work(struct work_struct *work)
{
	struct diff_area *diff_area =
		container_of(work, struct diff_area, cache_release_work);

	diff_area_cache_release(diff_area);
}

struct diff_area *diff_area_new(dev_t dev_id, struct diff_storage *diff_storage)
{
	int ret = 0;
	struct diff_area *diff_area = NULL;
#if defined(HAVE_BDEV_FILE_OPEN)
	struct file *bdev;
#elif defined(HAVE_BDEV_HANDLE)
	struct bdev_handle *bdev;
#else
	struct block_device *bdev;
#endif

	pr_debug("Open device [%u:%u]\n", MAJOR(dev_id), MINOR(dev_id));

#if defined(HAVE_BDEV_FILE_OPEN)
	bdev = bdev_file_open_by_dev(dev_id, FMODE_READ | FMODE_WRITE, NULL, NULL);
#elif defined(HAVE_BDEV_HANDLE)
	bdev = bdev_open_by_dev(dev_id, FMODE_READ | FMODE_WRITE, NULL, NULL);
#elif defined(HAVE_BLK_HOLDER_OPS)
	bdev = blkdev_get_by_dev(dev_id, FMODE_READ | FMODE_WRITE, NULL, NULL);
#else
	bdev = blkdev_get_by_dev(dev_id, FMODE_READ | FMODE_WRITE, NULL);
#endif
	if (IS_ERR(bdev)) {
		pr_err("Failed to open device. errno=%d\n",
		       abs((int)PTR_ERR(bdev)));
		return ERR_PTR(PTR_ERR(bdev));
	}

	diff_area = kzalloc(sizeof(struct diff_area), GFP_KERNEL);
	if (!diff_area) {
#if defined(HAVE_BDEV_FILE_OPEN)
		bdev_fput(bdev);
#elif defined(HAVE_BDEV_HANDLE)
		bdev_release(bdev);
#elif defined(HAVE_BLK_HOLDER_OPS)
		blkdev_put(bdev, NULL);
#else
		blkdev_put(bdev, FMODE_READ | FMODE_WRITE);
#endif
		return ERR_PTR(-ENOMEM);
	}
	memory_object_inc(memory_object_diff_area);

#if defined(HAVE_BDEV_FILE_OPEN)
	diff_area->orig_bdev_file = bdev;
	diff_area->orig_bdev = file_bdev(bdev);
#elif defined(HAVE_BDEV_HANDLE)
	diff_area->orig_bdev_handler = bdev;
	diff_area->orig_bdev = bdev->bdev;
#else
	diff_area->orig_bdev = bdev;
#endif
	diff_area->diff_storage = diff_storage;

	diff_area_calculate_chunk_size(diff_area);
	if (diff_area->chunk_shift > chunk_maximum_shift) {
		pr_info("The maximum allowable chunk size has been reached.\n");
		return ERR_PTR(-EFAULT);
	}
	pr_info("The optimal chunk size was calculated as %llu bytes for device [%d:%d]\n",
		 (1ull << diff_area->chunk_shift),
		 MAJOR(diff_area->orig_bdev->bd_dev),
		 MINOR(diff_area->orig_bdev->bd_dev));

	kref_init(&diff_area->kref);
	xa_init(&diff_area->chunk_map);

	if (!diff_storage->capacity) {
#ifdef CONFIG_BLK_SNAP_ALLOW_DIFF_STORAGE_IN_MEMORY
		diff_area->in_memory = true;
		pr_debug("Difference storage is empty.\n");
		pr_debug("Only the memory cache will be used to store the snapshots difference.\n");
#else
		pr_err("Difference storage is empty.\n");
		pr_err("In-memory difference storage is not supported");
		return ERR_PTR(-EFAULT);
#endif
	}

	spin_lock_init(&diff_area->chunk_map_lock);
	INIT_LIST_HEAD(&diff_area->read_cache_queue);
	atomic_set(&diff_area->read_cache_count, 0);
	INIT_LIST_HEAD(&diff_area->write_cache_queue);
	atomic_set(&diff_area->write_cache_count, 0);
	INIT_WORK(&diff_area->cache_release_work, diff_area_cache_release_work);

	spin_lock_init(&diff_area->free_diff_buffers_lock);
	INIT_LIST_HEAD(&diff_area->free_diff_buffers);
	atomic_set(&diff_area->free_diff_buffers_count, 0);

	atomic_set(&diff_area->corrupt_flag, 0);

#ifdef STANDALONE_BDEVFILTER
	atomic64_set(&diff_area->stat_processed, 0);
	atomic64_set(&diff_area->stat_copied, 0);
	atomic64_set(&diff_area->stat_processed, 0);
	atomic64_set(&diff_area->stat_image_written, 0);

	log_histogram_init(&diff_area->read_hg, 4096);
	log_histogram_init(&diff_area->redirect_hg, 4096);
#endif

	if (ret) {
		diff_area_put(diff_area);
		return ERR_PTR(ret);
	}

	atomic_set(&diff_area->corrupt_flag, 0);

	return diff_area;
}

static void diff_area_take_chunk_from_cache(struct diff_area *diff_area,
					    struct chunk *chunk)
{
	spin_lock(&diff_area->chunk_map_lock);
	if (!list_is_first(&chunk->link, &chunk->link)) {
		list_del_init(&chunk->link);

		if (chunk_state_check(chunk, CHUNK_ST_DIRTY)) {
#ifdef BLK_SNAP_DEBUG_IMAGE_WRITE
			pr_debug("Take chunk #%lu from write cache",
				 chunk->number);
#endif
			atomic_dec(&diff_area->write_cache_count);
		} else
			atomic_dec(&diff_area->read_cache_count);
	}
	spin_unlock(&diff_area->chunk_map_lock);
}

static struct chunk *diff_area_chunk_take(struct diff_area *diff_area,
					unsigned long nr)
{
	int ret;
	struct chunk *chunk, *new_chunk;

	spin_lock(&diff_area->chunk_map_lock);
	chunk = xa_load(&diff_area->chunk_map, nr);
	if (chunk)
		atomic_inc(&chunk->refcount);
	spin_unlock(&diff_area->chunk_map_lock);
	if (chunk)
		return chunk;

	new_chunk = chunk_alloc(diff_area, nr);
	if (!new_chunk) {
		pr_debug("Could not allocate a chunk to write the image\n");
		diff_area_set_corrupted(diff_area, -ENOMEM);
		return ERR_PTR(-ENOMEM);
	}

	spin_lock(&diff_area->chunk_map_lock);
	ret = xa_insert(&diff_area->chunk_map, nr, new_chunk, GFP_ATOMIC);
	if (likely(!ret)) {
		chunk = new_chunk;
		new_chunk = NULL;
	} else if (ret == -EBUSY) {
		/* another chunk has just been created */
		chunk_free(diff_area, new_chunk);
		chunk = xa_load(&diff_area->chunk_map, nr);
		if (likely(chunk)) {
			atomic_inc(&chunk->refcount);
			ret = 0;
		}
	}
	spin_unlock(&diff_area->chunk_map_lock);

	if (unlikely(ret)) {
		if (ret == -EBUSY) {
			pr_err("Could not get a chunk from chunk map\n");
			ret = -EINVAL;
		} else
			pr_err("Could not insert a chunk to chunk map\n");

		diff_area_set_corrupted(diff_area, ret);
		chunk = ERR_PTR(ret);
	}

	return chunk;
}

/**
 * diff_area_copy() - Implements the copy-on-write mechanism.
 *
 *
 */
int diff_area_copy(struct diff_area *diff_area, sector_t sector, sector_t count,
		   const bool is_nowait)
{
	int ret = 0;
	sector_t offset;
	struct chunk *chunk;
	struct diff_buffer *diff_buffer;
	sector_t area_sect_first;
	sector_t chunk_sectors = diff_area_chunk_sectors(diff_area);

#ifdef STANDALONE_BDEVFILTER
	atomic64_add(count, &diff_area->stat_processed);
#endif
	area_sect_first = round_down(sector, chunk_sectors);
	for (offset = area_sect_first; offset < (sector + count);
	     offset += chunk_sectors) {

		chunk = diff_area_chunk_take(diff_area,
				diff_area_chunk_number(diff_area, offset));
		if (!chunk)
			return PTR_ERR(chunk);

		if (is_nowait) {
			if (down_trylock(&chunk->lock))
				return -EAGAIN;
		} else {
			ret = down_killable(&chunk->lock);
			if (unlikely(ret))
				return ret;
		}
		chunk->diff_area = diff_area_get(diff_area);

		WARN_ON(diff_area_chunk_number(diff_area, offset) != chunk->number);
		if (chunk_state_check(chunk, CHUNK_ST_FAILED | CHUNK_ST_DIRTY |
						     CHUNK_ST_STORE_READY)) {
			/*
			 * The chunk has already been:
			 * - Failed, when the snapshot is corrupted
			 * - Overwritten in the snapshot image
			 * - Already stored in the diff storage
			 */
			chunk_up(chunk);
			continue;
		}

		if (unlikely(chunk_state_check(
			    chunk, CHUNK_ST_LOADING | CHUNK_ST_STORING))) {
			pr_err("Invalid chunk state\n");
			ret = -EFAULT;
			goto fail_unlock_chunk;
		}

		if (chunk_state_check(chunk, CHUNK_ST_BUFFER_READY)) {
			diff_area_take_chunk_from_cache(diff_area, chunk);
			/**
			 * The chunk has already been read, but now we need
			 * to store it to diff_storage.
			 */
			ret = chunk_schedule_storing(chunk, is_nowait);
			if (unlikely(ret))
				goto fail_unlock_chunk;
		} else {
			diff_buffer =
				diff_buffer_take(chunk->diff_area, is_nowait);
			if (IS_ERR(diff_buffer)) {
				ret = PTR_ERR(diff_buffer);
				goto fail_unlock_chunk;
			}
			WARN(chunk->diff_buffer, "Chunks buffer has been lost");
			chunk->diff_buffer = diff_buffer;

#ifdef STANDALONE_BDEVFILTER
			atomic64_add(chunk->sector_count, &diff_area->stat_copied);
#endif
			ret = chunk_async_load_orig(chunk, is_nowait);
			if (unlikely(ret))
				goto fail_unlock_chunk;
		}
	}

	return ret;
fail_unlock_chunk:
	WARN_ON(!chunk);
	chunk_store_failed(chunk, ret);
	return ret;
}

int diff_area_wait(struct diff_area *diff_area, sector_t sector, sector_t count,
		   const bool is_nowait)
{
	int ret = 0;
	sector_t offset;
	struct chunk *chunk;
	sector_t area_sect_first;
	sector_t chunk_sectors = diff_area_chunk_sectors(diff_area);

	area_sect_first = round_down(sector, chunk_sectors);
	for (offset = area_sect_first; offset < (sector + count);
	     offset += chunk_sectors) {
	     	spin_lock(&diff_area->chunk_map_lock);
		chunk = xa_load(&diff_area->chunk_map,
				diff_area_chunk_number(diff_area, offset));
		if (chunk)
			atomic_inc(&chunk->refcount);
		spin_unlock(&diff_area->chunk_map_lock);
		if (!chunk) {
			pr_err("Could not get a chunk from chunk map\n");
			diff_area_set_corrupted(diff_area, -EINVAL);
			return -EINVAL;
		}
		WARN_ON(diff_area_chunk_number(diff_area, offset) != chunk->number);
		if (is_nowait) {
			if (down_trylock(&chunk->lock)) {
				atomic_dec(&chunk->refcount);
				return -EAGAIN;
			}
		} else {
			ret = down_killable(&chunk->lock);
			if (unlikely(ret)) {
				atomic_dec(&chunk->refcount);
				return ret;
			}
		}
		chunk->diff_area = diff_area_get(diff_area);

		if (chunk_state_check(chunk, CHUNK_ST_FAILED )) {
			/*
			 * The chunk has already been:
			 * - Failed, when the snapshot is corrupted
			 * - Overwritten in the snapshot image
			 * - Already stored in the diff storage
			 */
			chunk_up(chunk);
			ret = -EFAULT;
			break;
		}

		if (chunk_state_check(chunk, CHUNK_ST_BUFFER_READY |
				      CHUNK_ST_DIRTY | CHUNK_ST_STORE_READY)) {
			/*
			 * The chunk has already been:
			 * - Read
			 * - Overwritten in the snapshot image
			 * - Already stored in the diff storage
			 */
			chunk_up(chunk);
			continue;
		}
	}

	return ret;
}

static inline void diff_area_image_put_chunk(struct diff_area_image_ctx *io_ctx)
{
	if (io_ctx->is_write) {
		/*
		 * Since the chunk was taken to perform writing,
		 * we mark it as dirty.
		 */
		chunk_state_set(io_ctx->chunk, CHUNK_ST_DIRTY);
#ifdef BLK_SNAP_DEBUG_IMAGE_WRITE
		pr_debug("chunk #%ld marked as dirty\n", io_ctx->chunk->number);
#endif
	}

	chunk_schedule_caching(io_ctx->chunk);
}

void diff_area_image_ctx_done(struct diff_area_image_ctx *io_ctx)
{
	if (!io_ctx->chunk)
		return;

	diff_area_image_put_chunk(io_ctx);
}

static int diff_area_load_chunk_from_storage(struct diff_area *diff_area,
					     struct chunk *chunk)
{
	struct diff_buffer *diff_buffer;

	diff_buffer = diff_buffer_take(diff_area, false);
	if (IS_ERR(diff_buffer))
		return PTR_ERR(diff_buffer);

	WARN_ON(chunk->diff_buffer);
	chunk->diff_buffer = diff_buffer;

	if (chunk_state_check(chunk, CHUNK_ST_STORE_READY)) {
#ifdef BLK_SNAP_DEBUG_IMAGE_WRITE
		pr_debug("Read chunk #%lu from diff storage", chunk->number);
#endif
		return chunk_load_diff(chunk);
	}

	return chunk_load_orig(chunk);
}

static struct chunk *diff_area_image_context_get_chunk(
	struct diff_area_image_ctx *io_ctx, sector_t sector)
{
	int ret;
	struct chunk *chunk;
	struct diff_area *diff_area = io_ctx->diff_area;
	unsigned long nr = diff_area_chunk_number(diff_area, sector);

	if (io_ctx->chunk) {
		if (io_ctx->chunk->number == nr)
			return io_ctx->chunk;

		/*
		 * If the sector falls into a new chunk, then we release
		 * the old chunk.
		 */
		diff_area_image_put_chunk(io_ctx);
		io_ctx->chunk = NULL;
	}

	/* Take a next chunk. */
	chunk = diff_area_chunk_take(diff_area, nr);

	ret = down_killable(&chunk->lock);
	if (ret) {
		atomic_dec(&chunk->refcount);
		return ERR_PTR(ret);
	}
	chunk->diff_area = diff_area_get(diff_area);

	if (unlikely(chunk_state_check(chunk, CHUNK_ST_FAILED))) {
		pr_err("Chunk #%ld corrupted\n", chunk->number);

		pr_debug("nr=%ld\n", nr);
		pr_debug("sector=%llu\n", sector);
		pr_debug("Chunk size %llu in bytes\n",
		       (1ull << diff_area->chunk_shift));
		pr_debug("Chunk count %lu\n", diff_area->chunk_count);

		ret = -EIO;
		goto fail_unlock_chunk;
	}

	/*
	 * If there is already data in the buffer, then nothing needs to be loaded.
	 * Otherwise, the chunk needs to be loaded from the original device or
	 * from the difference storage.
	 */
	if (!chunk_state_check(chunk, CHUNK_ST_BUFFER_READY)) {
		ret = diff_area_load_chunk_from_storage(diff_area, chunk);
		if (unlikely(ret))
			goto fail_unlock_chunk;

		/* Set the flag that the buffer contains the required data. */
		chunk_state_set(chunk, CHUNK_ST_BUFFER_READY);
	} else
		diff_area_take_chunk_from_cache(diff_area, chunk);

	io_ctx->chunk = chunk;
	return chunk;

fail_unlock_chunk:
	pr_err("Failed to load chunk #%ld\n", chunk->number);
	chunk_up(chunk);
	return ERR_PTR(ret);
}

/**
 * diff_area_image_io - Implements copying data from the chunk to bio_vec when
 *	reading or from bio_vec to the chunk when writing.
 */
blk_status_t diff_area_image_io(struct diff_area_image_ctx *io_ctx,
				const struct bio_vec *bvec, sector_t *pos)
{
	unsigned int bv_len = bvec->bv_len;
	struct iov_iter iter;

	iov_iter_bvec(&iter, io_ctx->is_write ? WRITE : READ, bvec, 1, bv_len);

	while (bv_len) {
		struct diff_buffer_iter diff_buffer_iter;
		struct chunk *chunk;
		size_t buff_offset;

		chunk = diff_area_image_context_get_chunk(io_ctx, *pos);
		if (IS_ERR(chunk))
			return BLK_STS_IOERR;

		buff_offset = (size_t)(*pos - chunk_sector(chunk))
				<< SECTOR_SHIFT;
		while (bv_len &&
		       diff_buffer_iter_get(chunk->diff_buffer, buff_offset,
					    &diff_buffer_iter)) {
			size_t sz;

			if (io_ctx->is_write)
				sz = copy_page_from_iter(
					diff_buffer_iter.page,
					diff_buffer_iter.offset,
					diff_buffer_iter.bytes,
					&iter);
			else
				sz = copy_page_to_iter(
					diff_buffer_iter.page,
					diff_buffer_iter.offset,
					diff_buffer_iter.bytes,
					&iter);
			if (!sz)
				return BLK_STS_IOERR;

			buff_offset += sz;
			*pos += (sz >> SECTOR_SHIFT);
			bv_len -= sz;
		}
	}

	return BLK_STS_OK;
}

static inline void diff_area_event_corrupted(struct diff_area *diff_area,
					     int err_code)
{
	struct blk_snap_event_corrupted data = {
		.orig_dev_id.mj = MAJOR(diff_area->orig_bdev->bd_dev),
		.orig_dev_id.mn = MINOR(diff_area->orig_bdev->bd_dev),
		.err_code = abs(err_code),
	};

	event_gen(&diff_area->diff_storage->event_queue, GFP_NOIO,
		  blk_snap_event_code_corrupted, &data,
		  sizeof(struct blk_snap_event_corrupted));
}

void diff_area_set_corrupted(struct diff_area *diff_area, int err_code)
{
	if (atomic_inc_return(&diff_area->corrupt_flag) != 1)
		return;

	diff_area_event_corrupted(diff_area, err_code);

	pr_err("Set snapshot device is corrupted for [%u:%u] with error code %d\n",
	       MAJOR(diff_area->orig_bdev->bd_dev),
	       MINOR(diff_area->orig_bdev->bd_dev), abs(err_code));
}

void diff_area_throttling_io(struct diff_area *diff_area)
{
}

#ifdef BLK_SNAP_DEBUG_SECTOR_STATE
int diff_area_get_sector_state(struct diff_area *diff_area, sector_t sector,
			       unsigned int *chunk_state)
{
	struct chunk *chunk;
	sector_t chunk_sectors = diff_area_chunk_sectors(diff_area);
	sector_t offset = round_down(sector, chunk_sectors);

	spin_lock(&diff_area->chunk_map_lock);
	chunk = xa_load(&diff_area->chunk_map, diff_area_chunk_number(diff_area, offset));
	if (chunk)
		atomic_inc(&chunk->refcount);
	spin_unlock(&diff_area->chunk_map_lock);
	if (!chunk)
		return -EINVAL;

	WARN_ON(diff_area_chunk_number(diff_area, offset) != chunk->number);
	down(&chunk->lock);
	*chunk_state = atomic_read(&chunk->state);
	chunk_up(chunk);

	return 0;
}

int diff_area_get_sector_image(struct diff_area *diff_area, sector_t pos,
			       void *buf)
{
	struct chunk *chunk;
	struct diff_area_image_ctx io_ctx;
	struct diff_buffer_iter diff_buffer_iter;

	diff_area_image_ctx_init(&io_ctx, diff_area, false);
	chunk = diff_area_image_context_get_chunk(&io_ctx, pos);
	if (IS_ERR(chunk))
		return PTR_ERR(chunk);

	diff_buffer_iter_get(chunk->diff_buffer, pos - chunk_sector(chunk),
			     &diff_buffer_iter);
	memcpy(buf,
	       page_address(diff_buffer_iter.page) + diff_buffer_iter.offset,
	       SECTOR_SIZE);

	diff_area_image_ctx_done(&io_ctx);
	return 0;
}

#endif
