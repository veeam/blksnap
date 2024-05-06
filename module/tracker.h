/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023 Veeam Software Group GmbH */
#ifndef __BLKSNAP_TRACKER_H
#define __BLKSNAP_TRACKER_H
#ifdef BLKSNAP_STANDALONE
#include "bdevfilter-internal.h"
#else
#include <linux/blk-filter.h>
#endif
#include <linux/kref.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/rwsem.h>
#include <linux/blkdev.h>
#include <linux/fs.h>

struct cbt_map;
struct diff_area;

/**
 * struct tracker - Tracker for a block device.
 *
 * @filter:
 *	The block device filter structure.
 * @orig_bdev:
 *	The original block device this trackker is attached to.
 * @ctl_lock:
 *	The mutex blocks simultaneous management of the tracker from different
 *	treads.
 * @link:
 *	List header. Allows to combine trackers into a list in a snapshot.
 * @kref:
 *	The reference counter allows to control the lifetime of the tracker.
 * @dev_id:
 *	Original block device ID.
 * @snapshot_is_taken:
 *	Indicates that a snapshot was taken for the device whose I/O unit are
 *	handled by this tracker.
 * @cbt_map:
 *	Pointer to a change block tracker map.
 * @diff_area:
 *	Pointer to a difference area.
 * @snap_disk:
 *	Snapshot image disk.
 *
 * The goal of the tracker is to handle I/O unit. The tracker detectes the range
 * of sectors that will change and transmits them to the CBT map and to the
 * difference area.
 */
struct tracker {
	struct blkfilter filter;
#ifdef BLKSNAP_STANDALONE
	char *orig_bdevpath;
#else
	struct block_device *orig_bdev;
#endif
	struct mutex ctl_lock;
	struct list_head link;
	dev_t dev_id;

	atomic_t snapshot_is_taken;

	struct cbt_map *cbt_map;
	struct diff_area *diff_area;
	struct gendisk *snap_disk;
};

int __init tracker_init(void);
void tracker_done(void);

static inline void tracker_put(struct tracker *tracker)
{
	if (likely(tracker))
		bdevfilter_put(&tracker->filter);
};
static inline void tracker_get(struct tracker *tracker)
{
	bdevfilter_get(&tracker->filter);
};
int tracker_take_snapshot(struct tracker *tracker);
void tracker_release_snapshot(struct tracker *tracker);

#endif /* __BLKSNAP_TRACKER_H */
