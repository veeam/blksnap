// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME "-chunk: " fmt

#include <linux/slab.h>
#include <linux/dm-io.h>
#include <linux/sched/mm.h>
#include "memory_checker.h"
#include "chunk.h"
#include "diff_io.h"
#include "diff_buffer.h"
#include "diff_area.h"
#include "diff_storage.h"
#ifdef STANDALONE_BDEVFILTER
#include "log.h"
#endif

extern int chunk_maximum_in_cache;

#ifdef BLK_SNAP_DEBUG_CHUNK_IO
DEFINE_MUTEX(logging_lock);
#endif

void chunk_diff_buffer_release(struct chunk *chunk)
{
	if (unlikely(!chunk->diff_buffer))
		return;

	chunk_state_unset(chunk, CHUNK_ST_BUFFER_READY);
	diff_buffer_release(chunk->diff_area, chunk->diff_buffer);
	chunk->diff_buffer = NULL;
}

void chunk_store_failed(struct chunk *chunk, int error)
{
	struct diff_area *diff_area = chunk->diff_area;

	chunk_state_set(chunk, CHUNK_ST_FAILED);
	chunk_diff_buffer_release(chunk);
	diff_storage_free_region(chunk->diff_region);
	chunk->diff_region = NULL;

	up(&chunk->lock);
	if (error)
		diff_area_set_corrupted(diff_area, error);
};

int chunk_schedule_storing(struct chunk *chunk, bool is_nowait)
{
	struct diff_area *diff_area = chunk->diff_area;

	if (WARN(!list_is_first(&chunk->cache_link, &chunk->cache_link),
		 "The chunk already in the cache"))
		return -EINVAL;

#ifdef BLK_SNAP_ALLOW_DIFF_STORAGE_IN_MEMORY
	if (diff_area->in_memory) {
		up(&chunk->lock);
		return 0;
	}
#endif
	if (!chunk->diff_region) {
		struct diff_region *diff_region;

		diff_region = diff_storage_new_region(
			diff_area->diff_storage,
			diff_area_chunk_sectors(diff_area));
		if (IS_ERR(diff_region)) {
			pr_debug("Cannot get store for chunk #%ld\n",
				 chunk->number);
			return PTR_ERR(diff_region);
		}

		chunk->diff_region = diff_region;
	}

	return chunk_async_store_diff(chunk, is_nowait);
}

void chunk_schedule_caching(struct chunk *chunk)
{
	int in_cache_count = 0;
	struct diff_area *diff_area = chunk->diff_area;

	might_sleep();

	spin_lock(&diff_area->caches_lock);

	/*
	 * The locked chunk cannot be in the cache.
	 * If the check reveals that the chunk is in the cache, then something
	 * is wrong in the algorithm.
	 */
	if (WARN(!list_is_first(&chunk->cache_link, &chunk->cache_link),
		 "The chunk already in the cache")) {
		spin_unlock(&diff_area->caches_lock);

		chunk_store_failed(chunk, 0);
		return;
	}

	if (chunk_state_check(chunk, CHUNK_ST_DIRTY)) {
		list_add_tail(&chunk->cache_link,
			      &diff_area->write_cache_queue);
		in_cache_count =
			atomic_inc_return(&diff_area->write_cache_count);
#ifdef BLK_SNAP_DEBUG_IMAGE_WRITE
		pr_debug("chunk #%ld should be stored. already in cache %d\n",
			 chunk->number, in_cache_count);
#endif
	} else {
		list_add_tail(&chunk->cache_link, &diff_area->read_cache_queue);
		in_cache_count =
			atomic_inc_return(&diff_area->read_cache_count);
	}
	spin_unlock(&diff_area->caches_lock);

	up(&chunk->lock);

	/* Initiate the cache clearing process */
#ifdef BLK_SNAP_DEBUG_IMAGE_WRITE
	if (atomic_read(&diff_area->write_cache_count) >
	    chunk_maximum_in_cache) {
		pr_debug(
			"Need to cleanup write cache: in_cache_count=%d, chunk_maximum_in_cache=%d\n",
			atomic_read(&diff_area->write_cache_count),
			chunk_maximum_in_cache);
	}
#endif
	if ((in_cache_count > chunk_maximum_in_cache))
		queue_work(system_wq, &diff_area->cache_release_work);
}

static void chunk_notify_load(void *ctx)
{
	struct chunk *chunk = ctx;
	int error = chunk->diff_io->error;

	diff_io_free(chunk->diff_io);
	chunk->diff_io = NULL;

#ifdef BLK_SNAP_DEBUG_CHUNK_IO
	mutex_lock(&logging_lock);
	pr_debug("DEBUG! loaded chunk #%ld \n", chunk->number);
	print_hex_dump(KERN_INFO, "data header: ", DUMP_PREFIX_OFFSET, 32, 1,
		       page_address(chunk->diff_buffer->pages[0]), 96, true);
	mutex_unlock(&logging_lock);
#endif
	might_sleep();

	if (unlikely(error)) {
		chunk_store_failed(chunk, error);
		goto out;
	}

	if (unlikely(chunk_state_check(chunk, CHUNK_ST_FAILED))) {
		pr_err("Chunk in a failed state\n");
		up(&chunk->lock);
		goto out;
	}

	if (chunk_state_check(chunk, CHUNK_ST_LOADING)) {
		int ret;
		unsigned int current_flag;

		chunk_state_unset(chunk, CHUNK_ST_LOADING);
		chunk_state_set(chunk, CHUNK_ST_BUFFER_READY);

		current_flag = memalloc_noio_save();
		ret = chunk_schedule_storing(chunk, false);
		memalloc_noio_restore(current_flag);
		if (ret)
			chunk_store_failed(chunk, ret);
		goto out;
	}

	pr_err("invalid chunk state 0x%x\n", atomic_read(&chunk->state));
	up(&chunk->lock);
out:
	atomic_dec(&chunk->diff_area->pending_io_count);
}

static void chunk_notify_store(void *ctx)
{
	struct chunk *chunk = ctx;
	int error = chunk->diff_io->error;

	diff_io_free(chunk->diff_io);
	chunk->diff_io = NULL;

	might_sleep();

	if (unlikely(error)) {
		chunk_store_failed(chunk, error);
		goto out;
	}

	if (unlikely(chunk_state_check(chunk, CHUNK_ST_FAILED))) {
		pr_err("Chunk in a failed state\n");
		chunk_store_failed(chunk, 0);
		goto out;
	}
	if (chunk_state_check(chunk, CHUNK_ST_STORING)) {
		chunk_state_unset(chunk, CHUNK_ST_STORING);
		chunk_state_set(chunk, CHUNK_ST_STORE_READY);

		if (chunk_state_check(chunk, CHUNK_ST_DIRTY)) {
			/*
			 * The chunk marked "dirty" was stored in the difference
			 * storage. Now it is processed in the same way as any
			 * other stored chunks.
			 * Therefore, the "dirty" mark can be removed.
			 */
			chunk_state_unset(chunk, CHUNK_ST_DIRTY);
#ifdef BLK_SNAP_DEBUG_IMAGE_WRITE
			pr_debug("chunk #%ld has been stored\n", chunk->number);
			pr_debug("Release buffer for chunk #%ld\n",
				 chunk->number);
#endif
			chunk_diff_buffer_release(chunk);
		} else {
			unsigned int current_flag;

			current_flag = memalloc_noio_save();
			chunk_schedule_caching(chunk);
			memalloc_noio_restore(current_flag);
			goto out;
		}
	} else
		pr_err("invalid chunk state 0x%x\n", atomic_read(&chunk->state));
	up(&chunk->lock);
out:
	atomic_dec(&chunk->diff_area->pending_io_count);
}

struct chunk *chunk_alloc(struct diff_area *diff_area, unsigned long number)
{
	struct chunk *chunk;

	chunk = kzalloc(sizeof(struct chunk), GFP_KERNEL);
	if (!chunk)
		return NULL;
	memory_object_inc(memory_object_chunk);

	INIT_LIST_HEAD(&chunk->cache_link);
	sema_init(&chunk->lock, 1);
	chunk->diff_area = diff_area;
	chunk->number = number;
	atomic_set(&chunk->state, 0);

	return chunk;
}

void chunk_free(struct chunk *chunk)
{
	if (unlikely(!chunk))
		return;

	down(&chunk->lock);
	chunk_diff_buffer_release(chunk);
	diff_storage_free_region(chunk->diff_region);
	chunk_state_set(chunk, CHUNK_ST_FAILED);
	up(&chunk->lock);

	kfree(chunk);
	memory_object_dec(memory_object_chunk);
}

/*
 * Starts asynchronous storing of a chunk to the  difference storage.
 */
int chunk_async_store_diff(struct chunk *chunk, bool is_nowait)
{
	int ret;
	struct diff_io *diff_io;
	struct diff_region *region = chunk->diff_region;

	if (WARN(!list_is_first(&chunk->cache_link, &chunk->cache_link),
		 "The chunk already in the cache"))
		return -EINVAL;

#ifdef BLK_SNAP_DEBUG_CHUNK_IO
	mutex_lock(&logging_lock);
	pr_debug("DEBUG! %s chunk #%ld sector=%llu count=%llu", __FUNCTION__,
		 chunk->number, region->sector, region->count);
	print_hex_dump(KERN_INFO, "data header: ", DUMP_PREFIX_OFFSET, 32, 1,
		       page_address(chunk->diff_buffer->pages[0]), 96, true);
	mutex_unlock(&logging_lock);
#endif
	diff_io = diff_io_new_async_write(chunk_notify_store, chunk, is_nowait);
	if (unlikely(!diff_io)) {
		if (is_nowait)
			return -EAGAIN;
		else
			return -ENOMEM;
	}

	WARN_ON(chunk->diff_io);
	chunk->diff_io = diff_io;
	chunk_state_set(chunk, CHUNK_ST_STORING);
	atomic_inc(&chunk->diff_area->pending_io_count);

	ret = diff_io_do(chunk->diff_io, region, chunk->diff_buffer, is_nowait);
	if (ret) {
		atomic_dec(&chunk->diff_area->pending_io_count);
		diff_io_free(chunk->diff_io);
		chunk->diff_io = NULL;
#ifdef BLK_SNAP_DEBUG_IMAGE_WRITE
		pr_debug("Failed to write diff for chunk #%ld\n",
			 chunk->number);
#endif
	}

	return ret;
}

/*
 * Starts asynchronous loading of a chunk from the original block device.
 */
int chunk_async_load_orig(struct chunk *chunk, const bool is_nowait)
{
	int ret;
	struct diff_io *diff_io;
	struct diff_region region = {
		.bdev = chunk->diff_area->orig_bdev,
		.sector = (sector_t)(chunk->number) *
			  diff_area_chunk_sectors(chunk->diff_area),
		.count = chunk->sector_count,
	};

#ifdef BLK_SNAP_DEBUG_CHUNK_IO
	pr_debug("DEBUG! %s chunk #%ld sector=%llu count=%llu", __FUNCTION__,
		 chunk->number, region.sector, region.count);
#endif
	diff_io = diff_io_new_async_read(chunk_notify_load, chunk, is_nowait);
	if (unlikely(!diff_io)) {
		if (is_nowait)
			return -EAGAIN;
		else
			return -ENOMEM;
	}

	WARN_ON(chunk->diff_io);
	chunk->diff_io = diff_io;
	chunk_state_set(chunk, CHUNK_ST_LOADING);
	atomic_inc(&chunk->diff_area->pending_io_count);

	ret = diff_io_do(chunk->diff_io, &region, chunk->diff_buffer, is_nowait);
	if (ret) {
		atomic_dec(&chunk->diff_area->pending_io_count);
		diff_io_free(chunk->diff_io);
		chunk->diff_io = NULL;
	}
	return ret;
}

/*
 * Performs synchronous loading of a chunk from the original block device.
 */
int chunk_load_orig(struct chunk *chunk)
{
	int ret;
	struct diff_io *diff_io;
	struct diff_region region = {
		.bdev = chunk->diff_area->orig_bdev,
		.sector = (sector_t)(chunk->number) *
			  diff_area_chunk_sectors(chunk->diff_area),
		.count = chunk->sector_count,
	};

	diff_io = diff_io_new_sync_read();
	if (unlikely(!diff_io))
		return -ENOMEM;

	ret = diff_io_do(diff_io, &region, chunk->diff_buffer, false);
	if (!ret)
		ret = diff_io->error;

	diff_io_free(diff_io);
	return ret;
}

/*
 * Performs synchronous loading of a chunk from the difference storage.
 */
int chunk_load_diff(struct chunk *chunk)
{
	int ret;
	struct diff_io *diff_io;
	struct diff_region *region = chunk->diff_region;

#ifdef BLK_SNAP_DEBUG_CHUNK_IO
	pr_debug("%s chunk #%ld sector=%llu count=%llu", __FUNCTION__,
		 chunk->number, region->sector, region->count);
#endif
	diff_io = diff_io_new_sync_read();
	if (unlikely(!diff_io))
		return -ENOMEM;

	ret = diff_io_do(diff_io, region, chunk->diff_buffer, false);
	if (!ret)
		ret = diff_io->error;

	diff_io_free(diff_io);
#ifdef BLK_SNAP_DEBUG_CHUNK_IO
	mutex_lock(&logging_lock);
	pr_debug("DEBUG! loaded chunk #%ld from diff area\n", chunk->number);
	print_hex_dump(KERN_INFO, "data header: ", DUMP_PREFIX_OFFSET, 32, 1,
		       page_address(chunk->diff_buffer->pages[0]), 96, true);
	mutex_unlock(&logging_lock);
#endif
	return ret;
}
