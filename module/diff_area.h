/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023 Veeam Software Group GmbH */
#ifndef __BLKSNAP_DIFF_AREA_H
#define __BLKSNAP_DIFF_AREA_H

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
struct tracker;

/**
 * struct diff_area - Describes the difference area for one original device.
 *
 * @kref:
 *	The reference counter allows to manage the lifetime of an object.
 * @orig_bdev:
 *	A pointer to the structure of an opened block device.
 * @diff_storage:
 *	Pointer to difference storage for storing difference data.
 * @tracker:
 *	Back pointer to the tracker for this &struct diff_area
 * @chunk_shift:
 *	Power of 2 used to specify the chunk size. This allows to set different
 *	chunk sizes for huge and small block devices.
 * @chunk_count:
 *	Count of chunks. The number of chunks into which the block device
 *	is divided.
 * @chunk_map:
 *	A map of chunks. The map stores only chunks of differences. Chunks are
 *	added to the map if this data block was overwritten on the original
 *	device, or was overwritten on the snapshot. If there is no chunk in the
 *	map, then when accessing the snapshot, I/O units are redirected to the
 *	original device.
 * @store_queue_lock:
 *	The spinlock guarantees consistency of the linked lists of chunks
 *	queue.
 * @store_queue:
 *	The queue of chunks waiting to be stored to the difference storage.
 * @store_queue_count:
 *	The number of chunks in the store queue.
 * @store_queue_work:
 *	The workqueue work item. This worker stores chunks to the difference
 *	storage freeing up the cache. It's limits the number of chunks that
 *	store their data in RAM.
 * @store_queue_processing:
 *	The flag is an indication that the &diff_area.store_queue_work is
 *	running or has been scheduled to run.
 * @free_diff_buffers_lock:
 *	The spinlock guarantees consistency of the linked lists of free
 *	difference buffers.
 * @free_diff_buffers:
 *	Linked list of free difference buffers allows to reduce the number
 *	of buffer allocation and release operations.
 * @free_diff_buffers_count:
 *	The number of free difference buffers in the linked list.
 * @image_io_queue_lock:
 *	The spinlock guarantees consistency of the linked lists of I/O
 *	requests to image.
 * @image_io_queue:
 *	A linked list of I/O units for the snapshot image that need to be read
 *	from the difference storage to process.
 * @image_io_work:
 *	A worker who maintains the I/O units for reading or writing data to the
 *	difference storage file. If the difference storage is a block device,
 *	then this worker is not	used to process the I/O units of the snapshot
 *	image.
 * @physical_blksz:
 *	The physical block size for the snapshot image is equal to the
 *	physical block size of the original device.
 * @logical_blksz:
 *	The logical block size for the snapshot image is equal to the
 *	logical block size of the original device.
 * @corrupt_flag:
 *	The flag is set if an error occurred in the operation of the data
 *	saving mechanism in the diff area. In this case, an error will be
 *	generated when reading from the snapshot image.
 * @error_code:
 *	The error code that caused the snapshot to be corrupted.
 *
 * The &struct diff_area is created for each block device in the snapshot. It
 * is used to store the differences between the original block device and the
 * snapshot image. That is, when writing data to the original device, the
 * differences are copied as chunks to the difference storage. Reading and
 * writing from the snapshot image is also performed using &struct diff_area.
 *
 * The map of chunks is a xarray. It has a capacity limit. This can be
 * especially noticeable on 32-bit systems. The maximum number of chunks for
 * 32-bit systems cannot be equal or more than 2^32.
 *
 * For example, for a 256 TiB disk and a chunk size of 65536 bytes, the number
 * of chunks in the chunk map will be equal to 2^32. This number already goes
 * beyond the 32-bit number. Therefore, for large disks, it is required to
 * increase the size of the chunk.
 *
 * The store queue allows to postpone the operation of storing a chunks data
 * to the difference storage and perform it later in the worker thread.
 *
 * The linked list of difference buffers allows to have a certain number of
 * "hot" buffers. This allows to reduce the number of allocations and releases
 * of memory.
 *
 * If it is required to read or write to the difference storage file to process
 * I/O unit from snapshot image, then this operation is performed in a separate
 * thread. To do this, a worker &diff_area.image_io_work and a queue
 * &diff_area.image_io_queue are used. An attempt to read a file from the same
 * thread that initiated the block I/O can lead to a deadlock state.
 */
struct diff_area {
	struct kref kref;
	struct block_device *orig_bdev;
	struct diff_storage *diff_storage;
	struct tracker *tracker;

	unsigned long chunk_shift;
	unsigned long chunk_count;
	struct xarray chunk_map;

	spinlock_t store_queue_lock;
	struct list_head store_queue;
	atomic_t store_queue_count;
	struct work_struct store_queue_work;
	bool store_queue_processing;

	spinlock_t free_diff_buffers_lock;
	struct list_head free_diff_buffers;
	atomic_t free_diff_buffers_count;

	spinlock_t image_io_queue_lock;
	struct list_head image_io_queue;
	struct work_struct image_io_work;

	unsigned int physical_blksz;
	unsigned int logical_blksz;

	unsigned long corrupt_flag;
	int error_code;
};

struct diff_area *diff_area_new(struct tracker *tracker,
				struct diff_storage *diff_storage);
void diff_area_free(struct kref *kref);
static inline struct diff_area *diff_area_get(struct diff_area *diff_area)
{
	kref_get(&diff_area->kref);
	return diff_area;
};
static inline void diff_area_put(struct diff_area *diff_area)
{
	kref_put(&diff_area->kref, diff_area_free);
};

void diff_area_set_corrupted(struct diff_area *diff_area, int err_code);
static inline bool diff_area_is_corrupted(struct diff_area *diff_area)
{
	return !!diff_area->corrupt_flag;
};
static inline sector_t diff_area_chunk_sectors(struct diff_area *diff_area)
{
	return (sector_t)(1ull << (diff_area->chunk_shift - SECTOR_SHIFT));
};
bool diff_area_cow(struct bio *bio, struct diff_area *diff_area,
		   struct bvec_iter *iter);

bool diff_area_submit_chunk(struct diff_area *diff_area, struct bio *bio);
void diff_area_rw_chunk(struct kref *kref);

#endif /* __BLKSNAP_DIFF_AREA_H */
