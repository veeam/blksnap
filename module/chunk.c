// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME "-chunk: " fmt
#include <linux/slab.h>
#include <linux/dm-io.h>
#include <linux/sched/mm.h>
#ifdef CONFIG_DEBUG_MEMORY_LEAK
#include "memory_checker.h"
#endif
#include "params.h"
#include "chunk.h"
#include "diff_io.h"
#include "diff_buffer.h"
#include "diff_area.h"
#include "diff_storage.h"

#ifdef CONFIG_DEBUGLOG
#undef pr_debug
#define pr_debug(fmt, ...) \
	printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
#endif

static inline
void chunk_store_failed(struct chunk *chunk, int error)
{
	struct diff_area *diff_area = chunk->diff_area;

	chunk_state_set(chunk, CHUNK_ST_FAILED);
	if (chunk->diff_buffer) {
		diff_buffer_release(chunk->diff_area, chunk->diff_buffer);
		chunk->diff_buffer = NULL;
	}
	diff_storage_free_store(chunk->diff_store);

	mutex_unlock(&chunk->lock);
	diff_area_set_corrupted(diff_area, error);
};

void chunk_schedule_storing(struct chunk *chunk)
{
	int ret;
	struct diff_area *diff_area = chunk->diff_area;


	//pr_debug("Schedule storing chunk #%ld\n", chunk->number);
	might_sleep();
	WARN_ON(!mutex_is_locked(&chunk->lock));

	if (diff_area->in_memory) {
		mutex_unlock(&chunk->lock);
		return;
	}

	if (!chunk->diff_store) {
		struct diff_region *diff_store;

		diff_store = diff_storage_new_store(
				diff_area->diff_storage,
				diff_area_chunk_sectors(diff_area));
		if (unlikely(IS_ERR(diff_store))) {
			chunk_store_failed(chunk, PTR_ERR(diff_store));
			pr_debug("Cannot get store for chunk #%ld\n", chunk->number);
			return;
		}

		chunk->diff_store = diff_store;
	}

	ret = chunk_async_store_diff(chunk);
	if (ret)
		chunk_store_failed(chunk, ret);
}

void chunk_schedule_caching(struct chunk *chunk)
{
	bool need_to_cleanup = false;
	struct diff_area *diff_area = chunk->diff_area;

	might_sleep();
	WARN_ON(!mutex_is_locked(&chunk->lock));

	//pr_debug("Add chunk #%ld to cache\n", chunk->number);
	spin_lock(&diff_area->cache_list_lock);
	if (!chunk_state_check(chunk, CHUNK_ST_IN_CACHE)) {
		chunk_state_set(chunk, CHUNK_ST_IN_CACHE);
		list_add_tail(&chunk->cache_link, &diff_area->caching_chunks);
		need_to_cleanup =
			atomic_inc_return(&diff_area->caching_chunks_count) >
			chunk_maximum_in_cache;
	}
	spin_unlock(&diff_area->cache_list_lock);

	mutex_unlock(&chunk->lock);

	// Initiate the cache clearing process.
	if (need_to_cleanup) {
//#ifdef CONFIG_DEBUG_DIFF_BUFFER
//		pr_debug("Need to cleanup cache: caching_chunks_count=%d, chunk_maximum_in_cache=%d\n",
//			atomic_read(&diff_area->caching_chunks_count),
//			chunk_maximum_in_cache);
//#endif
		queue_work(system_wq, &diff_area->caching_chunks_work);
	}
}

static
void chunk_notify_load(void *ctx)
{
	struct chunk *chunk = ctx;
	int error = chunk->diff_io->error;

	diff_io_free(chunk->diff_io);
	chunk->diff_io = NULL;

#ifdef CONFIG_DEBUG_CHUNK_IO
	pr_debug("DEBUG! loaded chunk #%ld \n", chunk->number);
	print_hex_dump(KERN_INFO, "data header: ", DUMP_PREFIX_ADDRESS, 16, 1,
		page_address(chunk->diff_buffer->pages[0]), 64, true);
#endif
	might_sleep();
	WARN_ON(!mutex_is_locked(&chunk->lock));

	if (unlikely(error)) {
		chunk_store_failed(chunk, error);
		goto out;
	}

	if (unlikely(chunk_state_check(chunk, CHUNK_ST_FAILED))) {
		pr_err("Chunk in a failed state\n");
		mutex_unlock(&chunk->lock);
		goto out;
	}

	if (chunk_state_check(chunk, CHUNK_ST_LOADING)) {
		unsigned int current_flag;

		chunk_state_unset(chunk, CHUNK_ST_LOADING);
		chunk_state_set(chunk, CHUNK_ST_BUFFER_READY);

		current_flag = memalloc_noio_save();
		chunk_schedule_storing(chunk);
		memalloc_noio_restore(current_flag);
		goto out;
	}

	pr_err("%s - Invalid chunk state 0x%x\n", __FUNCTION__, atomic_read(&chunk->state));
	mutex_unlock(&chunk->lock);
out:
	atomic_dec(&chunk->diff_area->pending_io_count);
	return;
}

static
void chunk_notify_store(void *ctx)
{
	struct chunk *chunk = ctx;
	int error = chunk->diff_io->error;

	diff_io_free(chunk->diff_io);
	chunk->diff_io = NULL;

	might_sleep();
	WARN_ON(!mutex_is_locked(&chunk->lock));

	if (unlikely(error)) {
		chunk_store_failed(chunk, error);
		goto out;
	}

	if (unlikely(chunk_state_check(chunk, CHUNK_ST_FAILED))) {
		pr_err("Chunk in a failed state\n");
		mutex_unlock(&chunk->lock);
		goto out;
	}
	if (chunk_state_check(chunk, CHUNK_ST_STORING)) {
		unsigned int current_flag;

		chunk_state_unset(chunk, CHUNK_ST_STORING);
		chunk_state_set(chunk, CHUNK_ST_STORE_READY);

		current_flag = memalloc_noio_save();
		chunk_schedule_caching(chunk);
		memalloc_noio_restore(current_flag);
		goto out;
	}

	pr_err("%s - Invalid chunk state 0x%x\n", __FUNCTION__, atomic_read(&chunk->state));
	mutex_unlock(&chunk->lock);
out:
	atomic_dec(&chunk->diff_area->pending_io_count);
	return;
}

struct chunk *chunk_alloc(struct diff_area *diff_area, unsigned long number)
{
	struct chunk *chunk;

	chunk = kzalloc(sizeof(struct chunk), GFP_KERNEL);
	if (!chunk)
		return NULL;
#ifdef CONFIG_DEBUG_MEMORY_LEAK
	memory_object_inc(memory_object_chunk);
#endif
	INIT_LIST_HEAD(&chunk->cache_link);
	mutex_init(&chunk->lock);
	chunk->diff_area = diff_area;
	chunk->number = number;
	atomic_set(&chunk->state, 0);

	return chunk;
}

void chunk_free(struct chunk *chunk)
{
	if (unlikely(!chunk))
		return;
//#ifdef CONFIG_DEBUG_DIFF_BUFFER
//	if (mutex_is_locked(&chunk->lock))
//		pr_debug("Chunk %ld locked", chunk->number);
//#endif
	mutex_lock(&chunk->lock);
	if (chunk->diff_buffer) {
		diff_buffer_release(chunk->diff_area, chunk->diff_buffer);
		chunk->diff_buffer = NULL;
	}
	diff_storage_free_store(chunk->diff_store);
	chunk_state_set(chunk, CHUNK_ST_FAILED);
	mutex_unlock(&chunk->lock);

	kfree(chunk);
#ifdef CONFIG_DEBUG_MEMORY_LEAK
	memory_object_dec(memory_object_chunk);
#endif
}

/**
 * chunk_async_store_diff() - Starts asynchronous storing of a chunk to the
 *	difference storage.
 *
 */
int chunk_async_store_diff(struct chunk *chunk)
{
	int ret;
	struct diff_io *diff_io;
	struct diff_region *region = chunk->diff_store;

#ifdef CONFIG_DEBUG_CHUNK_IO
	pr_debug("%s %s sector=%llu count=%llu", __FUNCTION__,
		region->bdev->bd_device.kobj.name, region->sector, region->count);
	pr_debug("DEBUG! stored chunk #%ld \n", chunk->number);
	print_hex_dump(KERN_INFO, "data header: ", DUMP_PREFIX_ADDRESS, 16, 1,
		page_address(chunk->diff_buffer->pages[0]), 64, true);
#endif
	diff_io = diff_io_new_async_write(chunk_notify_store, chunk, false);
	if (unlikely(!diff_io))
		return -ENOMEM;

	WARN_ON(chunk->diff_io);
	chunk->diff_io = diff_io;
	chunk_state_set(chunk, CHUNK_ST_STORING);
	atomic_inc(&chunk->diff_area->pending_io_count);

	ret = diff_io_do(chunk->diff_io, region, chunk->diff_buffer, false);
	if (ret) {
		atomic_dec(&chunk->diff_area->pending_io_count);
		diff_io_free(chunk->diff_io);
		chunk->diff_io = NULL;
	}

	return ret;
}

/**
 * chunk_asunc_load_orig() - Starts asynchronous loading of a chunk from
 * 	the origian block device.
 */
int chunk_asunc_load_orig(struct chunk *chunk, bool is_nowait)
{
	int ret;
	struct diff_io *diff_io;
	struct diff_region region = {
		.bdev = chunk->diff_area->orig_bdev,
		.sector = (sector_t)(chunk->number) * diff_area_chunk_sectors(chunk->diff_area),
		.count = chunk->sector_count,
	};

#ifdef CONFIG_DEBUG_CHUNK_IO
	pr_debug("%s %s sector=%llu count=%llu", __FUNCTION__,
		region.bdev->bd_device.kobj.name, region.sector, region.count);
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

/**
 * chunk_load_orig() - Performs synchronous loading of a chunk from the
 * 	original block device.
 */
int chunk_load_orig(struct chunk *chunk)
{
	int ret;
	struct diff_io *diff_io;
	struct diff_region region = {
		.bdev = chunk->diff_area->orig_bdev,
		.sector = (sector_t)(chunk->number) * diff_area_chunk_sectors(chunk->diff_area),
		.count = chunk->sector_count,
	};
#ifdef CONFIG_DEBUG_CHUNK_IO
	pr_debug("%s", __FUNCTION__);
	pr_debug("sector=%llu\n", region.sector);
	pr_debug("count=%llu\n", region.count);
#endif
	diff_io = diff_io_new_sync_read();
	if (unlikely(!diff_io))
		return -ENOMEM;

	ret = diff_io_do(diff_io, &region, chunk->diff_buffer, false);
	if (!ret)
		ret = diff_io->error;

	diff_io_free(diff_io);
	return ret;
}

/**
 * chunk_load_diff() - Performs synchronous loading of a chunk from the
 * 	difference storage.
 */
int chunk_load_diff(struct chunk *chunk)
{
	int ret;
	struct diff_io *diff_io;
	struct diff_region *region = chunk->diff_store;

#ifdef CONFIG_DEBUG_CHUNK_IO
	pr_debug("%s %s sector=%llu count=%llu", __FUNCTION__,
		region->bdev->bd_device.kobj.name, region->sector, region->count);
#endif
	diff_io = diff_io_new_sync_read();
	if (unlikely(!diff_io))
		return -ENOMEM;

	ret = diff_io_do(diff_io, region, chunk->diff_buffer, false);
	if (!ret)
		ret = diff_io->error;

	diff_io_free(diff_io);
#ifdef CONFIG_DEBUG_CHUNK_IO
	pr_debug("DEBUG! loaded chunk #%ld from diff area\n", chunk->number);
	print_hex_dump(KERN_INFO, "data header: ", DUMP_PREFIX_ADDRESS, 16, 1,
		page_address(chunk->diff_buffer->pages[0]), 64, true);
#endif
	return ret;
}
