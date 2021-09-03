/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "event_queue.h"

struct dm_io_client;
struct diff_storage;

struct diff_area {
	struct kref kref;
	struct block_device *orig_bdev;
	struct dm_io_client *io_client;
	/**
	 * chunk_size_shift - power of 2 for chunk size
	 * It's allow to set various chunk size for huge and small block device.
	 */
	unsigned long long chunk_size_shift;
	/**
	 * chunk_count - count of chunks
	 * the number of chunks into which the block device is divided.
	 */
	unsigned long chunk_count;
	/**
	 * chunk_map - a map of chunk
	 * xarray can be too little on x32 systems. This creates a limit
	 * in the size of supported disks to 256 TB with a chunk size
	 * of 65536 bytes. In addition, such a large array will take up too
	 * much space in memory.
	 * Therefore, the size of the chunk should be selected so that
	 * the size of the map is not too large, and the index does not
	 * exceed 32 bits.
	 */
	struct xarray chunk_map;
	/**
	 * Same diff_area can use one diff_storage for storing his chunks
	 */
	struct diff_storage *diff_storage;

	/**
	 * storing_chunks - a list of chunks that are waiting to be store
	 * sectors from memory to diff storage
	 * 
	 * Reading data from the original block device is performed in the
	 * context of the thread in which the filtering is performed.
	 * But storing data to diff storage is performed in workqueue.
	 * The chunks that need to be stored in diff storage are accumitale
	 * into the diff_store_changs list.
	 */
	struct list_head storing_chunks;
	spinlock_t storing_chunks_lock;
	/**
	 * storing_chunks_work - kworkers work item
	 * Saves chunks in diff storage
	 */
	struct work_struct storing_chunks_work;

	/**
	 * After copying the sectors from the original block device to diff
	 * storage, the sectors are still located in memory.
	 * When the snapshot data is read or written, they also remain in
	 * memory for some time.
	 */
	struct list_head caching_chunks;
	spinlock_t caching_chunks_lock;
	atomic_t caching_chunks_count;
	/**
	 * caching_chunks_work - kworkers work item
	 * Saves chunks in diff storage
	 */
	struct work_struct caching_chunks_work;

	atomic_t corrupted_flag;
};

struct diff_area *diff_area_new(dev_t dev_id, struct diff_storage *diff_storage, struct event_queue *event_queue);
void diff_area_free(struct kref *kref);
static inline void diff_area_get(struct diff_area *diff_area)
{
	kref_get(diff_area->kref);
};
static inline void diff_area_put(struct diff_area *diff_area)
{
	if (likely(diff_area))
		kref_put(diff_area->kref, diff_area_free);
};

int diff_area_copy(struct diff_area *diff_area, sector_t sector, sector_t count
                   bool is_nowait);


struct chunk;
struct diff_area_image_context {
	struct diff_area *diff_area;
	bool is_write;
	unsigned long number;
	struct chunk* chunk;
};

void diff_area_image_context_init(struct diff_area_image_context *image_ctx,
                                  void *disks_private_data);
void diff_area_image_context_done(struct diff_area_image_context *image_ctx);

blk_status_t diff_area_image_write(struct diff_area_image_context *image_ctx,
				   struct page *page, unsigned int page_off,
				   sector_t sector, unsigned int len);
blk_status_t diff_area_image_read(struct diff_area_image_context *image_ctx,
				  struct page *page, unsigned int page_off,
				  sector_t sector, unsigned int len);
