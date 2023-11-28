/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023 Veeam Software Group GmbH */
#ifndef __BLKSNAP_DIFF_STORAGE_H
#define __BLKSNAP_DIFF_STORAGE_H

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
 * @bdev:
 *	A pointer to the block device that has been selected for the
 *	difference storage. Available only if configuration BLKSNAP_DIFF_BLKDEV
 *	is enabled.
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
#if defined(CONFIG_BLKSNAP_DIFF_BLKDEV)
	struct block_device *bdev;
#endif
	struct file *file;
	sector_t capacity;
	sector_t limit;
	sector_t filled;
	sector_t requested;

	atomic_t low_space_flag;
	atomic_t overflow_flag;

	struct work_struct reallocate_work;
	struct event_queue event_queue;
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

int diff_storage_set_diff_storage(struct diff_storage *diff_storage,
				  unsigned int fd, sector_t limit);

int diff_storage_alloc(struct diff_storage *diff_storage, sector_t count,
#if defined(CONFIG_BLKSNAP_DIFF_BLKDEV)
		       struct block_device **bdev,
#endif
		       struct file **file, sector_t *sector);
#endif /* __BLKSNAP_DIFF_STORAGE_H */
