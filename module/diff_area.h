/* SPDX-License-Identifier: GPL-2.0 */
#pragma once
#include <linux/slab.h>
#include <linux/uio.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>
#include <linux/xarray.h>
#include "event_queue.h"

struct diff_storage;
struct chunk;

/**
 * struct diff_area - Discribes the difference area for one original device.
 * @kref:
 *	The reference counter. The &struct diff_area can be shared between
 *	the &struct tracker and &struct snapimage.
 * @orig_bdev:
 *	A pointer to the structure of an opened block device.
 * @diff_storage:
 *	Pointer to difference storage for storing difference data.
 * @chunk_shift:
 *	Power of 2 used to specify the chunk size. This allows to set different chunk sizes for
 *	huge and small block devices.
 * @chunk_count:
 *	Count of chunks. The number of chunks into which the block device
 *	is divided.
 * @chunk_map:
 *	A map of chunks.
 * @in_memory:
 *	A sign that difference storage is not prepared and all differences are
 *	stored in RAM.
 * @caches_lock:
 *	This spinlock guarantees consistency of the linked lists of chunk
 *	caches.
 * @read_cache_queue:
 *	Queue for the read cache.
 * @read_cache_count:
 *	The number of chunks in the read cache.
 * @write_cache_queue:
 *	Queue for the write cache.
 * @write_cache_count:
 *	The number of chunks in the write cache.
 * @cache_release_work:
 *	The workqueue work item. This worker limits the number of chunks
 *	that store their data in RAM.
 * @free_diff_buffers_lock:
 *	This spinlock guarantees consistency of the linked lists of
 *	free difference buffers.
 * @free_diff_buffers:
 *	Linked list of free difference buffers allows to reduce the number
 *	of buffer allocation and release operations.
 * @free_diff_buffers_count:
 *	The number of free difference buffers in the linked list.
 * @corrupt_flag:
 *	The flag is set if an error occurred in the operation of the data
 *	saving mechanism in the diff area. In this case, an error will be
 *	generated when reading from the snapshot image.
 * @pending_io_count:
 *	Counter of incomplete I/O operations. Allows to wait for all I/O
 *	operations to be completed before releasing this structure.
 *
 * The &struct diff_area is created for each block device in the snapshot.
 * It is used to save the differences between the original block device and
 * the snapshot image. That is, when writing data to the original device,
 * the differences are copied as chunks to the difference storage.
 * Reading and writing from the snapshot image is also performed using
 * &struct diff_area.
 *
 * The xarray has a limit on the maximum size. This can be especially
 * noticeable on 32-bit systems. This creates a limit in the size of
 * supported disks.
 *
 * For example, for a 256 TiB disk with a block size of 65536 bytes, the
 * number of elements in the chunk map will be equal to 2 with a power of 32.
 * Therefore, the number of chunks into which the block device is divided is
 * limited.
 *
 * To provide high performance, a read cache and a write cache for chunks are
 * used. The cache algorithm is the simplest. If the data of the chunk was
 * read to the difference buffer, then the buffer is not released immediately,
 * but is placed at the end of the queue. The worker thread checks the number
 * of chunks in the queue and releases a difference buffer for the first chunk
 * in the queue, but only if the binary semaphore of the chunk is not locked.
 * If the read thread accesses the chunk from the cache again, it returns
 * back to the end of the queue.
 *
 * The linked list of difference buffers allows to have a certain number of
 * "hot" buffers. This allows to reduce the number of allocations and releases
 * of memory.
 *
 *
 */
struct diff_area {
	struct kref kref;

	struct block_device *orig_bdev;
	struct diff_storage *diff_storage;

	unsigned long long chunk_shift;
	unsigned long chunk_count;
	struct xarray chunk_map;
#ifdef CONFIG_BLK_SNAP_ALLOW_DIFF_STORAGE_IN_MEMORY
	bool in_memory;
#endif
	spinlock_t caches_lock;
	struct list_head read_cache_queue;
	atomic_t read_cache_count;
	struct list_head write_cache_queue;
	atomic_t write_cache_count;
	struct work_struct cache_release_work;

	spinlock_t free_diff_buffers_lock;
	struct list_head free_diff_buffers;
	atomic_t free_diff_buffers_count;

	atomic_t corrupt_flag;
	atomic_t pending_io_count;
};

struct diff_area *diff_area_new(dev_t dev_id,
				struct diff_storage *diff_storage);
void diff_area_free(struct kref *kref);
static inline void diff_area_get(struct diff_area *diff_area)
{
	kref_get(&diff_area->kref);
};
static inline void diff_area_put(struct diff_area *diff_area)
{
	if (likely(diff_area))
		kref_put(&diff_area->kref, diff_area_free);
};
void diff_area_set_corrupted(struct diff_area *diff_area, int err_code);
static inline bool diff_area_is_corrupted(struct diff_area *diff_area)
{
	return !!atomic_read(&diff_area->corrupt_flag);
};
static inline sector_t diff_area_chunk_sectors(struct diff_area *diff_area)
{
	return (sector_t)(1ull << (diff_area->chunk_shift - SECTOR_SHIFT));
};
int diff_area_copy(struct diff_area *diff_area, sector_t sector, sector_t count,
		   const bool is_nowait);

int diff_area_wait(struct diff_area *diff_area, sector_t sector, sector_t count,
                   const bool is_nowait);
/**
 * struct diff_area_image_ctx - The context for processing an io request to
 *	the snapshot image.
 * @diff_area:
 *	Pointer to &struct diff_area for the current snapshot image.
 * @is_write:
 *	Distinguishes between the behavior of reading or writing when
 *	processing a request.
 * @chunk:
 *	Current chunk.
 */
struct diff_area_image_ctx {
	struct diff_area *diff_area;
	bool is_write;
	struct chunk *chunk;
};

static inline void diff_area_image_ctx_init(struct diff_area_image_ctx *io_ctx,
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

/**
 *
 */
void diff_area_throttling_io(struct diff_area *diff_area);

#ifdef BLK_SNAP_DEBUG_SECTOR_STATE
/**
 *
 */
int diff_area_get_sector_state(struct diff_area *diff_area, sector_t sector,
			       unsigned int *chunk_state);
int diff_area_get_sector_image(struct diff_area *diff_area, sector_t pos,
			       void *buf);
#endif
