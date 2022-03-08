/* SPDX-License-Identifier: GPL-2.0 */
#pragma once
#include <linux/kref.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>
#include <linux/blkdev.h>
#include <linux/fs.h>

struct cbt_map;
struct diff_area;

/**
 * struct tracker - Tracker for block device.
 *
 * @kref:
 *	Protects the structure from being released during the processing of
 *	a IOCTL.
 * @dev_id:
 *	Original block device ID.
 * @snapshot_is_taken:
 *
 * @cbt_map:
 *
 * @diff_area:
 *
 */
struct tracker {
	struct kref kref;
	dev_t dev_id;

	atomic_t snapshot_is_taken;

	struct cbt_map *cbt_map;
	struct diff_area *diff_area;
};

void tracker_free(struct kref *kref);
static inline void tracker_put(struct tracker *tracker)
{
	if (likely(tracker))
		kref_put(&tracker->kref, tracker_free);
};
struct tracker *tracker_get_by_dev(struct block_device *bdev);

void tracker_done(void);

struct tracker *tracker_create_or_get(dev_t dev_id);
int tracker_remove(dev_t dev_id);
int tracker_collect(int max_count, struct blk_snap_cbt_info *cbt_info,
		    int *pcount);
int tracker_read_cbt_bitmap(dev_t dev_id, unsigned int offset, size_t length,
			    char __user *user_buff);
int tracker_mark_dirty_blocks(dev_t dev_id,
			      struct blk_snap_block_range *block_ranges,
			      unsigned int count);

int tracker_take_snapshot(struct tracker *tracker);
void tracker_release_snapshot(struct tracker *tracker);
