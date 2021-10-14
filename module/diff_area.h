/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <linux/slab.h>
#include <linux/uio.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>
#include <linux/xarray.h>
#include <linux/dm-io.h>
#include <linux/workqueue.h>
#include "event_queue.h"

struct diff_storage;
struct chunk;

/**
 * struct diff_area - Discribes the difference area for one original device.
 * @kref:
 *	This structure can be shared between &struct tracker and &struct
 *	snapimage.
 * @orig_bdev:
 *	A pointer to the structure of an open block device.
 * @io_client:
 *	dm-io is used for disk read and write operations.
 * @diff_storage:
 *	The same &struct diff_area can use the same diff_storage to store
 *	its data.
 * @chunk_shift:
 *	Power of 2 for chunk size. It's allow to set various chunk size for
 *	huge and small block device.
 * @chunk_count:
 *	Count of chunks. The number of chunks into which the block device
 *	is divided.
 * @chunk_map:
 *	A map of chunk. xarray can be too little on x32 systems. This creates
 *	a limit in the size of supported disks to 256 TB with a chunk size of
 *	65536 bytes. In addition, such a large array will take up too much
 *	space in memory.
 *	Therefore, the size of the chunk should be selected so that the size
 *	of the map is not too large, and the index does not exceed 32 bits.
 * @storing_chunks:
 *	A list of chunks that are waiting to be store sectors from memory to
 *	diff storage.
 *	Reading data from the original block device is performed in the context
 *	of the thread in which the filtering is performed. But storing data to
 *	diff storage is performed in workqueue.
 *	The chunks that need to be stored in diff storage are accumitale in
 *	this list.
 * @storage_list_lock:
 * 	Spin lock for the @storing_chunks list.
 * cache_list_lock:
 *      Spin lock for the @caching_chunks list.
 * @storing_chunks_work:
 *	The workqueue work item. This worker saves the chunks to the diff
 * 	storage.
 * @caching_chunks:
 *	After copying the sectors from the original block device to diff
 *	storage, the sectors are still located in memory. When the snapshot
 *	data is read or written, they also remain in memory for some time.
 * @caching_chunks_count:
 *	The number of chunks in the cache.
 * @caching_chunks_work:
 *	The workqueue work item which controls the cache size.
 * @corrupt_flag:
 *	The flag is set if an error occurred in the operation of the data
 *	saving mechanism in the diff area. In this case, an error will be
 *	generated when reading from the snapshot image.
 *
 * The &struct diff_area is created for each block device in the snapshot.
 * It is used to save the differences between the original block device and
 * the snapshot image. That is, when writing data to the original device,
 * the differences are copied as chunks to the difference storage.
 * Reading and writing from the snapshot image are also performed using
 * &struct diff_area.
 */
struct diff_area {
	struct kref kref;

	struct block_device *orig_bdev;
	struct dm_io_client *io_client;
	struct diff_storage *diff_storage;

	unsigned long long chunk_shift;
	unsigned long chunk_count;
	struct xarray chunk_map;

	bool in_memory;

	spinlock_t cache_list_lock;
	struct list_head caching_chunks;
	atomic_t caching_chunks_count;
	struct work_struct caching_chunks_work;

	atomic_t corrupt_flag;
};

struct diff_area *diff_area_new(dev_t dev_id, struct diff_storage *diff_storage);
void diff_area_free(struct kref *kref);
static inline
void diff_area_get(struct diff_area *diff_area)
{
	kref_get(&diff_area->kref);
};
static inline
void diff_area_put(struct diff_area *diff_area)
{
	if (likely(diff_area))
		kref_put(&diff_area->kref, diff_area_free);
};
void diff_area_set_corrupted(struct diff_area *diff_area, int err_code);
static inline
bool diff_area_is_corrupted(struct diff_area *diff_area)
{
	return !!atomic_read(&diff_area->corrupt_flag);
};
static inline
sector_t diff_area_chunk_sectors(struct diff_area *diff_area)
{
	return (sector_t)(1ULL << (diff_area->chunk_shift - SECTOR_SHIFT));
};
int diff_area_copy(struct diff_area *diff_area, sector_t sector, sector_t count,
		   bool is_nowait);

/**
 * struct diff_area_image_ctx - The context for processing an io request to
 * 	the snapshot image.
 * @diff_area:
 *	Pointer to &struct diff_area for current snapshot image.
 * @is_write:
 *	Distinguishes between the behavior of reading or writing when
 *	processing a request.
 * @chunk:
 *	Current chunk.
 */
struct diff_area_image_ctx {
	struct diff_area *diff_area;
	bool is_write;
	struct chunk* chunk;
};

static inline
void diff_area_image_ctx_init(struct diff_area_image_ctx *io_ctx,
			      struct diff_area *diff_area,
			      bool is_write)
{
	io_ctx->diff_area = diff_area;
	io_ctx->is_write = is_write;
	io_ctx->chunk = NULL;
};
void diff_area_image_ctx_done(struct diff_area_image_ctx *io_ctx);
blk_status_t diff_area_image_io(struct diff_area_image_ctx *io_ctx,
				const struct bio_vec *bvec, sector_t *pos);
