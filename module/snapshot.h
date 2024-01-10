/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023 Veeam Software Group GmbH */
#ifndef __BLKSNAP_SNAPSHOT_H
#define __BLKSNAP_SNAPSHOT_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/kref.h>
#include <linux/uuid.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>
#include <linux/fs.h>
#include "event_queue.h"

struct tracker;
struct diff_storage;
/**
 * struct snapshot - Snapshot structure.
 * @link:
 *	The list header allows to store snapshots in a linked list.
 * @kref:
 *	Protects the structure from being released during the processing of
 *	an ioctl.
 * @id:
 *	UUID of snapshot.
 * @rw_lock:
 *	Protects the structure from being modified by different threads.
 * @is_taken:
 *	Flag that the snapshot was taken.
 * @diff_storage:
 *	A pointer to the difference storage of this snapshot.
 * @trackers:
 *	List of block device trackers.
 *
 * A snapshot corresponds to a single backup session and provides snapshot
 * images for multiple block devices. Several backup sessions can be performed
 * at the same time, which means that several snapshots can exist at the same
 * time. However, the original block device can only belong to one snapshot.
 * Creating multiple snapshots from the same block device is not allowed.
 */
struct snapshot {
	struct list_head link;
	struct kref kref;
	uuid_t id;

	struct rw_semaphore rw_lock;

	bool is_taken;
	struct diff_storage *diff_storage;
	struct list_head trackers;
};

void __exit snapshot_done(void);

int snapshot_create(const char *filename, sector_t limit_sect,
		    struct blksnap_uuid *id);
int snapshot_destroy(const uuid_t *id);
int snapshot_add_device(const uuid_t *id, struct tracker *tracker);
int snapshot_take(const uuid_t *id);
int snapshot_collect(unsigned int *pcount,
		     struct blksnap_uuid __user *id_array);
struct event *snapshot_wait_event(const uuid_t *id, unsigned long timeout_ms);

#endif /* __BLKSNAP_SNAPSHOT_H */
