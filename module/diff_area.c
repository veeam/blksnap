// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME "-diff-area: " fmt
#include <linux/genhd.h>
#include <linux/slab.h>
#ifdef BLK_SNAP_DEBUG_MEMORY_LEAK
#include "memory_checker.h"
#endif
#include "params.h"
#include "blk_snap.h"
#include "chunk.h"
#include "diff_area.h"
#include "diff_buffer.h"
#include "diff_storage.h"
#include "diff_io.h"

#ifdef BLK_SNAP_DEBUGLOG
#undef pr_debug
#define pr_debug(fmt, ...) \
	printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
#endif

#ifndef HAVE_BDEV_NR_SECTORS
static inline
sector_t bdev_nr_sectors(struct block_device *bdev)
{
	return i_size_read(bdev->bd_inode) >> 9;
};
#endif

static inline
unsigned long chunk_number(struct diff_area *diff_area, sector_t sector)
{
	return (unsigned long)(sector >> (diff_area->chunk_shift - SECTOR_SHIFT));
};

static inline
sector_t chunk_sector(struct chunk *chunk)
{
	return (sector_t)(chunk->number) << (chunk->diff_area->chunk_shift - SECTOR_SHIFT);
}

static inline
void recalculate_last_chunk_size(struct chunk *chunk)
{
	sector_t capacity;

	capacity = bdev_nr_sectors(chunk->diff_area->orig_bdev);
	if (capacity > round_down(capacity, chunk->sector_count))
		chunk->sector_count = capacity - round_down(capacity, chunk->sector_count);
}

static inline
unsigned long long count_by_shift(sector_t capacity, unsigned long long shift)
{
	return round_up(capacity, 1ull << (shift - SECTOR_SHIFT)) >> (shift - SECTOR_SHIFT);
}

static
void diff_area_calculate_chunk_size(struct diff_area *diff_area)
{
	unsigned long long shift = chunk_minimum_shift;
	unsigned long long count;
	sector_t capacity;
	sector_t min_io_sect;

	min_io_sect = (sector_t)(bdev_io_min(diff_area->orig_bdev) >> SECTOR_SHIFT);
	capacity = bdev_nr_sectors(diff_area->orig_bdev);

	count = count_by_shift(capacity, shift);
	while ((count > chunk_maximum_count) || (diff_area_chunk_sectors(diff_area) < min_io_sect)) {
		shift = shift << 1;
		count = count_by_shift(capacity, shift);
	}

	diff_area->chunk_shift = shift;
	diff_area->chunk_count = count;

	pr_info("The optimal chunk size was calculated as %llu bytes for device [%d:%d]\n",
		(1ull << diff_area->chunk_shift),
		MAJOR(diff_area->orig_bdev->bd_dev),
		MINOR(diff_area->orig_bdev->bd_dev));
}



void diff_area_free(struct kref *kref)
{
	unsigned long inx;
	u64 start_waiting;
	struct chunk *chunk;
	struct diff_area *diff_area = container_of(kref, struct diff_area, kref);

	inx = 0;
	start_waiting = jiffies_64;
	while(atomic_read(&diff_area->pending_io_count)) {
		schedule_timeout_interruptible(1);
		if (jiffies_64 > (start_waiting + HZ)) {
			start_waiting = jiffies_64;
			inx++;
			pr_warn("Waiting for pending I/O to complete\n");
			if (inx > 5) {
				pr_err("Failed to complete pending I/O\n");
				break;
			}
		}
	}

	//flush_work(&diff_area->storing_chunks_work);
	flush_work(&diff_area->caching_chunks_work);
	//flush_work(&diff_area->corrupt_work);

	xa_for_each(&diff_area->chunk_map, inx, chunk) {
		chunk_free(chunk);
	}
	xa_destroy(&diff_area->chunk_map);

	if (diff_area->orig_bdev) {
		blkdev_put(diff_area->orig_bdev, FMODE_READ | FMODE_WRITE);
		diff_area->orig_bdev = NULL;
	}

	/* Cleanup free_diff_buffers */
	diff_buffer_cleanup(diff_area);

	kfree(diff_area);
#ifdef BLK_SNAP_DEBUG_MEMORY_LEAK
	memory_object_dec(memory_object_diff_area);
#endif
}

static inline
bool too_many_chunks_in_cache(struct diff_area *diff_area)
{
	return atomic_read(&diff_area->caching_chunks_count) > chunk_maximum_in_cache;
}

static
struct chunk *chunk_get_from_cache_and_write_lock(struct diff_area *diff_area)
{
	struct chunk *iter;
	struct chunk *chunk = NULL;
#ifdef BLK_SNAP_DEBUG_DIFF_BUFFER
	int locked_mutex_counter = 0;
#endif
	spin_lock(&diff_area->cache_list_lock);
	list_for_each_entry(iter, &diff_area->caching_chunks, cache_link) {
		if (mutex_trylock(&iter->lock)) {
			chunk = iter;
			break;
		}
		/*
		 * If it is not possible to lock a chunk for writing,
		 * then it is currently in use and we try to cleanup
		 * next chunk.
		 */
#ifdef BLK_SNAP_DEBUG_DIFF_BUFFER
		locked_mutex_counter++;
#endif
	}
	if (likely(chunk)) {
		list_del(&chunk->cache_link);
		chunk_state_unset(chunk, CHUNK_ST_IN_CACHE);
		atomic_dec(&diff_area->caching_chunks_count);
	}
	spin_unlock(&diff_area->cache_list_lock);
#ifdef BLK_SNAP_DEBUG_DIFF_BUFFER
	if (locked_mutex_counter)
		pr_debug("Found %d locked chunk\n", locked_mutex_counter);
#endif
	return chunk;
}

static
void diff_area_caching_chunks_work(struct work_struct *work)
{
	struct diff_area *diff_area = container_of(work, struct diff_area, caching_chunks_work);
	struct chunk *chunk;

	while (too_many_chunks_in_cache(diff_area)) {
		chunk = chunk_get_from_cache_and_write_lock(diff_area);
		if (!chunk) {
#ifdef BLK_SNAP_DEBUG_DIFF_BUFFER
			pr_debug("Cannot get chunk. All is locked\n");
#endif
			break;
		}

//#ifdef BLK_SNAP_DEBUG_DIFF_BUFFER
//		pr_debug("Free buffer for chunk #%ld\n", chunk->number);
//		pr_debug("caching_chunks_count=%d. chunk_maximum_in_cache=%d\n",
//			atomic_read(&diff_area->caching_chunks_count),
//			chunk_maximum_in_cache);
//#endif
		WARN_ON(!mutex_is_locked(&chunk->lock));
		if (chunk_state_check(chunk, CHUNK_ST_BUFFER_READY)) {
			chunk_state_unset(chunk, CHUNK_ST_BUFFER_READY);
			diff_buffer_release(chunk->diff_area, chunk->diff_buffer);
			chunk->diff_buffer = NULL;
		} else {
			WARN_ON(chunk->diff_buffer);
		}
		mutex_unlock(&chunk->lock);
	}
}

struct diff_area *diff_area_new(dev_t dev_id, struct diff_storage *diff_storage)
{
	int ret = 0;
	struct diff_area *diff_area = NULL;
	struct block_device *bdev;
	unsigned long number;
	struct chunk *chunk;

	pr_debug("Open device [%u:%u]\n", MAJOR(dev_id), MINOR(dev_id));

	bdev = blkdev_get_by_dev(dev_id, FMODE_READ | FMODE_WRITE, NULL);
	if (IS_ERR(bdev)) {
		pr_err("Failed to open device. errno=%d\n", abs((int)PTR_ERR(bdev)));
		return ERR_PTR(PTR_ERR(bdev));
	}

	diff_area = kzalloc(sizeof(struct diff_area), GFP_KERNEL);
	if (!diff_area) {
		blkdev_put(bdev, FMODE_READ | FMODE_WRITE);
		return ERR_PTR(-ENOMEM);
	}
#ifdef BLK_SNAP_DEBUG_MEMORY_LEAK
	memory_object_inc(memory_object_diff_area);
#endif
	diff_area->orig_bdev = bdev;
	diff_area->diff_storage = diff_storage;

	diff_area_calculate_chunk_size(diff_area);
	pr_debug("Chunk size %llu in bytes\n", 1ULL << diff_area->chunk_shift);
	pr_debug("Chunk count %lu\n", diff_area->chunk_count);

	kref_init(&diff_area->kref);
	xa_init(&diff_area->chunk_map);

	if (!diff_storage->capacity) {
		diff_area->in_memory = true;
		pr_debug("Difference storage is empty.\n") ;
		pr_debug("Only the memory cache will be used to store the snapshots difference.\n") ;
	}

	spin_lock_init(&diff_area->cache_list_lock);
	INIT_LIST_HEAD(&diff_area->caching_chunks);
	INIT_WORK(&diff_area->caching_chunks_work, diff_area_caching_chunks_work);
	atomic_set(&diff_area->pending_io_count, 0);

        spin_lock_init(&diff_area->free_diff_buffers_lock);
        INIT_LIST_HEAD(&diff_area->free_diff_buffers);

	/**
	 * Allocating all chunks in advance allows not to do this in
	 * the process of filtering bio.
	 * In addition, the chunk structure has an rw semaphore that allows
	 * to lock data of a single chunk.
	 * Different threads can read, write, or dump their data to diff storage
	 * independently of each other, provided that different chunks are used.
	 */
	for (number = 0; number < diff_area->chunk_count; number++) {
		chunk = chunk_alloc(diff_area, number);
		if (!chunk) {
			pr_err("Failed allocate chunk\n");
			ret = -ENOMEM;
			break;
		}
		chunk->sector_count = diff_area_chunk_sectors(diff_area);

		ret = xa_insert(&diff_area->chunk_map, number, chunk, GFP_KERNEL);
		if (ret) {
			pr_err("Failed insert chunk to chunk map\n");
			chunk_free(chunk);
			break;
		}
	}
	if (ret) {
		diff_area_put(diff_area);
		return ERR_PTR(ret);
	}

	recalculate_last_chunk_size(chunk);

	atomic_set(&diff_area->corrupt_flag, 0);

	return diff_area;
}

/**
 * diff_area_copy() - Implements the copy-on-write mechanism.
 *
 *
 */
int diff_area_copy(struct diff_area *diff_area, sector_t sector, sector_t count,
		   bool is_nowait)
{
	int ret = 0;
	sector_t offset;
	struct chunk *chunk;
	struct diff_buffer *diff_buffer;
	sector_t area_sect_first;
	sector_t chunk_sectors = diff_area_chunk_sectors(diff_area);

	area_sect_first = round_down(sector, chunk_sectors);
	for (offset = area_sect_first; offset < (sector + count); offset += chunk_sectors) {
		chunk = xa_load(&diff_area->chunk_map, chunk_number(diff_area, offset));
		if (!chunk) {
			diff_area_set_corrupted(diff_area, -EINVAL);
			return -EINVAL;
		}
		WARN_ON(chunk_number(diff_area, offset) != chunk->number);
		if (is_nowait) {
			if (!mutex_trylock(&chunk->lock))
				return -EAGAIN;
		} else
			mutex_lock(&chunk->lock);

		if (chunk_state_check(chunk, CHUNK_ST_FAILED | CHUNK_ST_DIRTY | CHUNK_ST_STORE_READY)) {
			/*
			 * The сhunk has already been:
			 * - failed, when snapshot corrupted,
			 * - overwritten in the image of a snapshot,
			 * - already store in diff storage.
			 */
			WARN_ON(!mutex_is_locked(&chunk->lock));
			mutex_unlock(&chunk->lock);
			continue;
		}

		if (unlikely(chunk_state_check(chunk, CHUNK_ST_LOADING | CHUNK_ST_STORING))) {
			pr_err("Invalid chunk state\n");
			ret = -EFAULT;
			goto fail_unlock_chunk;
		}

		if (chunk_state_check(chunk, CHUNK_ST_BUFFER_READY)) {
			/**
			 * The chunk has already been read, but now we need
			 * to store it to diff_storage.
			 */
			ret = chunk_schedule_storing(chunk, is_nowait);
			if (unlikely(ret))
				goto fail_unlock_chunk;
		} else {
			if (is_nowait) {
				diff_buffer = diff_buffer_take(chunk->diff_area, GFP_NOIO | GFP_NOWAIT);
				if (IS_ERR(diff_buffer)) {
					ret = -EAGAIN;
					goto fail_unlock_chunk;
				}
			} else {
				diff_buffer = diff_buffer_take(chunk->diff_area, GFP_NOIO);
				if (IS_ERR(diff_buffer)) {
					ret = PTR_ERR(diff_buffer);
					goto fail_unlock_chunk;
				}
			}
			WARN_ON(chunk->diff_buffer);
			chunk->diff_buffer = diff_buffer;

			ret = chunk_asunc_load_orig(chunk, is_nowait);
			if (unlikely(ret))
				goto fail_unlock_chunk;
		}
	}

	return ret;
fail_unlock_chunk:
	WARN_ON(!chunk);
	WARN_ON(!mutex_is_locked(&chunk->lock));
	chunk_store_failed(chunk, ret);
	return ret;
}

static inline
void diff_area_image_put_chunk(struct chunk* chunk, bool is_write)
{
	if (is_write) {
		int ret;
//#ifdef BLK_SNAP_DEBUG_DIFF_BUFFER
//		pr_debug("chunk #%ld should be stored\n", chunk->number);
//		pr_debug("chunk buffer #%d \n", chunk->diff_buffer->number);
//#endif
		ret = chunk_schedule_storing(chunk, false);
		if (ret)
			chunk_store_failed(chunk, ret);
	}
	else {
//#ifdef BLK_SNAP_DEBUG_DIFF_BUFFER
//		pr_debug("chunk #%ld should be in cache\n", chunk->number);
//#endif
		chunk_schedule_caching(chunk);
	}
}

void diff_area_image_ctx_done(struct diff_area_image_ctx *io_ctx)
{
	if (!io_ctx->chunk)
		return;

	diff_area_image_put_chunk(io_ctx->chunk, io_ctx->is_write);
}

static
struct chunk* diff_area_image_context_get_chunk(struct diff_area_image_ctx *io_ctx,
						sector_t sector)
{
	int ret;
	struct chunk* chunk = io_ctx->chunk;
	struct diff_area *diff_area = io_ctx->diff_area;
	unsigned long new_chunk_number = chunk_number(diff_area, sector);

	if (chunk) {
		if (chunk->number == new_chunk_number)
			return chunk;

		/*
		 * If the sector falls into a new chunk, then we release
		 * the old chunk.
		 */
		diff_area_image_put_chunk(chunk, io_ctx->is_write);
		io_ctx->chunk = NULL;
	}


	//pr_err("Take chunk #%ld\n", new_chunk_number);
	/* Take a next chunk. */
	chunk = xa_load(&diff_area->chunk_map, new_chunk_number);
	if (unlikely(!chunk))
		return ERR_PTR(-EINVAL);

	mutex_lock(&chunk->lock);

	if (unlikely(chunk_state_check(chunk, CHUNK_ST_FAILED))) {
		pr_err("Chunk #%ld corrupted\n", chunk->number);
#ifdef BLK_SNAP_DEBUGLOG
		pr_err("new_chunk_number=%ld\n", new_chunk_number);
		pr_err("sector=%llu\n", sector);
		pr_err("Chunk size %llu in bytes\n", (1ULL << diff_area->chunk_shift));
		pr_err("Chunk count %lu\n", diff_area->chunk_count);
#endif
		ret = -EIO;
		goto fail_unlock_chunk;
	}

	/*
	 * If there is already data in the buffer, then nothing needs to be load.
	 * Otherwise, the chunk needs to be load from the original device or
	 * from diff storage.
	 */
	if (!chunk_state_check(chunk, CHUNK_ST_BUFFER_READY)) {
		struct diff_buffer *diff_buffer;

		diff_buffer = diff_buffer_take(diff_area, GFP_NOIO);
		if (IS_ERR(diff_buffer)) {
			ret = PTR_ERR(diff_buffer);
			goto fail_unlock_chunk;
		}
		WARN_ON(chunk->diff_buffer);
		chunk->diff_buffer = diff_buffer;

		if (chunk_state_check(chunk, CHUNK_ST_STORE_READY)) {
			//pr_debug("DEBUG! load diff chunk #%ld\n", chunk->number);
			ret = chunk_load_diff(chunk);
		}
		else {
			//pr_err("load orig chunk #%ld\n", chunk->number);
			ret = chunk_load_orig(chunk);
		}

		if (unlikely(ret))
			goto fail_unlock_chunk;

		/* Set the flag that the buffer contains the required data. */
		chunk_state_set(chunk, CHUNK_ST_BUFFER_READY);
	} else {
//#ifdef BLK_SNAP_DEBUG_DIFF_BUFFER
//		if (chunk_state_check(chunk, CHUNK_ST_STORE_READY))
//			pr_debug("DEBUG! load diff chunk #%ld buffer_number=%d from cache\n",
//				chunk->number, chunk->diff_buffer->number);
//#endif
		spin_lock(&diff_area->cache_list_lock);
		if (chunk_state_check(chunk, CHUNK_ST_IN_CACHE))
			list_move_tail(&chunk->cache_link, &diff_area->caching_chunks);
		spin_unlock(&diff_area->cache_list_lock);
	}

	if (io_ctx->is_write) {
		/**
		 * Since the chunk was taken to perform a writing,
		 * we mark it as dirty.
		 */
		chunk_state_set(chunk, CHUNK_ST_DIRTY);
		//pr_debug("DEBUG! chunk #%ld marked as dirty\n", chunk->number);
	}

	io_ctx->chunk = chunk;
	return chunk;

fail_unlock_chunk:
	pr_err("Failed to load chunk #%ld\n", chunk->number);
	WARN_ON(!mutex_is_locked(&chunk->lock));
	mutex_unlock(&chunk->lock);
	return ERR_PTR(ret);
}

static inline
sector_t diff_area_chunk_start(struct diff_area *diff_area, struct chunk *chunk)
{
	return (sector_t)(chunk->number) << diff_area->chunk_shift;
}

/**
 * diff_area_image_io - Implements copying data from chunk to bio_vec when
 * 	reading or from bio_tec to chunk when writing.
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
		sector_t buff_offset;

		chunk = diff_area_image_context_get_chunk(io_ctx, *pos);
		if (IS_ERR(chunk))
			return BLK_STS_IOERR;

		buff_offset = *pos - chunk_sector(chunk);
		while(bv_len && diff_buffer_iter_get(chunk->diff_buffer,
						     buff_offset, &diff_buffer_iter)) {
			ssize_t sz;

			if (io_ctx->is_write)
				sz = copy_page_from_iter(diff_buffer_iter.page,
							 diff_buffer_iter.offset,
							 diff_buffer_iter.bytes,
							 &iter);
			else
				sz = copy_page_to_iter(diff_buffer_iter.page,
						       diff_buffer_iter.offset,
						       diff_buffer_iter.bytes,
						       &iter);
			if (!sz)
				return BLK_STS_IOERR;

			buff_offset += (sz >> SECTOR_SHIFT);
			*pos += (sz >> SECTOR_SHIFT);
			bv_len -= sz;
		}
	}

	return BLK_STS_OK;
}

static inline
void diff_area_event_corrupted(struct diff_area *diff_area, int err_code)
{
	struct blk_snap_event_corrupted data = {
		.orig_dev_id.mj = MAJOR(diff_area->orig_bdev->bd_dev),
		.orig_dev_id.mn = MINOR(diff_area->orig_bdev->bd_dev),
		.err_code = abs(err_code),
	};

	event_gen(&diff_area->diff_storage->event_queue, GFP_NOIO,
		blk_snap_event_code_corrupted,
		&data, sizeof(struct blk_snap_event_corrupted));
}

void diff_area_set_corrupted(struct diff_area *diff_area, int err_code)
{
	if (atomic_inc_return(&diff_area->corrupt_flag) != 1)
		return;

	diff_area_event_corrupted(diff_area, err_code);

	pr_err("Set snapshot device is corrupted for [%u:%u] with error code %d\n",
	       MAJOR(diff_area->orig_bdev->bd_dev),
	       MINOR(diff_area->orig_bdev->bd_dev),
	       abs(err_code));
}

void diff_area_throttling_io(struct diff_area *diff_area)
{
	u64 start_waiting;

	start_waiting = jiffies_64;
	while(atomic_read(&diff_area->pending_io_count)) {
		schedule_timeout_interruptible(0);
		if (jiffies_64 > (start_waiting + HZ/10))
			break;
	}
}

#ifdef BLK_SNAP_DEBUG_SECTOR_STATE
int diff_area_get_sector_state(struct diff_area *diff_area, sector_t sector, unsigned int *chunk_state)
{
	struct chunk *chunk;
	sector_t chunk_sectors = diff_area_chunk_sectors(diff_area);
	sector_t offset = round_down(sector, chunk_sectors);

	chunk = xa_load(&diff_area->chunk_map, chunk_number(diff_area, offset));
	if (!chunk)
		return -EINVAL;

	WARN_ON(chunk_number(diff_area, offset) != chunk->number);
	mutex_lock(&chunk->lock);
	*chunk_state = atomic_read(&chunk->state);
	mutex_unlock(&chunk->lock);

	return 0;
}
#endif
