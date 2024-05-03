/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023 Veeam Software Group GmbH */
#ifndef __BLKSNAP_DIFF_STORAGE_H
#define __BLKSNAP_DIFF_STORAGE_H

#include <linux/xarray.h>
#include "event_queue.h"
struct blksnap_sectors;

/**
 * struct diff_storage - Difference storage.
 *
 * @kref:
 *	The reference counter.
 * @lock:
 *	Spinlock allows to safely change structure fields in a multithreaded
 *	environment.
 * @dev_id:
 *	ID of the block device on which the difference storage file is located.
 * @bdev_handle:
 *	A pointer to the block device handle. This handle allows to keep the
 *	block device open.
 * @bdev:
 *	A pointer to the block device that has been selected for the
 *	difference storage.
 * @file:
 *	A pointer to the file that was selected for the difference storage.
 * @capacity:
 *	Total amount of available difference storage space.
 * @limit:
 *	The limit to which the difference storage can be allowed to grow.
 * @filled:
 *	The number of sectors already filled in.
 * @requested:
 *	The number of sectors already requested from user space.
 * @low_space_flag:
 *	The flag is set if the number of free regions available in the
 *	difference storage is less than the allowed minimum.
 * @overflow_flag:
 *	The request for a free region failed due to the absence of free
 *	regions in the difference storage.
 * @reallocate_work:
 *	The working thread in which the difference storage file is growing.
 * @event_queue:
 *	A queue of events to pass events to user space.
 *
 * The difference storage manages the block device or file that are used
 * to store the data of the original block devices in the snapshot.
 * The difference storage is created one per snapshot and is used to store
 * data from all block devices.
 *
 * The difference storage file has the ability to increase while holding the
 * snapshot as needed within the specified limits. This is done using the
 * function vfs_fallocate().
 *
 * Changing the file size leads to a change in the file metadata in the file
 * system, which leads to the generation of I/O units for the block device.
 * Using a separate working thread ensures that metadata changes will be
 * handled and correctly processed by the block-level filters.
 *
 * The event queue allows to inform the user land about changes in the state
 * of the difference storage.
 */
struct diff_storage {
	struct kref kref;
	spinlock_t lock;

	dev_t dev_id;
	bdev_holder_t *bdev_holder;
	struct file *file;
	sector_t capacity;
	sector_t limit;
	sector_t filled;
	sector_t requested;

	atomic_t low_space_flag;
	atomic_t overflow_flag;

	struct work_struct reallocate_work;
	struct event_queue event_queue;
#ifdef BLKSNAP_MODIFICATION
	struct xarray diff_storage_bdev_map;
	spinlock_t ranges_lock;
	struct list_head free_ranges_list;
#endif
};

struct diff_storage *diff_storage_new(void);
void diff_storage_free(struct kref *kref);

static inline void diff_storage_get(struct diff_storage *diff_storage)
{
	kref_get(&diff_storage->kref);
};
static inline void diff_storage_put(struct diff_storage *diff_storage)
{
	if (likely(diff_storage))
		kref_put(&diff_storage->kref, diff_storage_free);
};

int diff_storage_set(struct diff_storage *diff_storage, const char *filename,
		     sector_t limit);

int diff_storage_alloc(struct diff_storage *diff_storage, sector_t count,
		       struct block_device **bdev, struct file **file,
		       sector_t *sector);

#ifdef BLKSNAP_MODIFICATION

int diff_storage_add_bdev(struct diff_storage *diff_storage,
			  bdev_holder_t *bdev_holder);

int diff_storage_add_range(struct diff_storage *diff_storage,
			      dev_t dev_id,
			      struct blksnap_sectors range);
int diff_storage_get_range(struct diff_storage *diff_storage, sector_t count,
			   struct block_device **pbdev, sector_t *poffset);
#endif

#endif /* __BLKSNAP_DIFF_STORAGE_H */
