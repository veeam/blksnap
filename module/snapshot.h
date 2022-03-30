/* SPDX-License-Identifier: GPL-2.0 */
#pragma once
#include <linux/types.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/kref.h>
#include <linux/uuid.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>
#include <linux/fs.h>
#include "big_buffer.h"
#include "event_queue.h"

struct tracker;
struct diff_storage;
struct snapimage;
/**
 * struct snapshot - Snapshot structure.
 * @link:
 *	The list header allows to store snapshots in a linked list.
 * @kref:
 *	Protects the structure from being released during the processing of
 *	an ioctl.
 * @id:
 *	UUID of snapshot.
 * @is_taken:
 *	Flag that the snapshot was taken.
 * @diff_storage:
 *	A pointer to the difference storage of this snapshot.
 * @count:
 *	The number of block devices in the snapshot. This number
 *	corresponds to the size of arrays of pointers to trackers
 *	and snapshot images.
 * @tracker_array:
 *	Array of pointers to block device trackers.
 * @snapimage_array:
 *	Array of pointers to images of snapshots of block devices.
 *
 * A snapshot corresponds to a single backup session and provides snapshot
 * images for multiple block devices. Several backup sessions can be
 * performed at the same time, which means that several snapshots can
 * exist at the same time. However, the original block device can only
 * belong to one snapshot. Creating multiple snapshots from the same block
 * device is not allowed.
 *
 * A UUID is used to identify the snapshot.
 *
 */
struct snapshot {
	struct list_head link;
	struct kref kref;
	uuid_t id;
	bool is_taken;
	struct diff_storage *diff_storage;
	int count;
	struct tracker **tracker_array;
	struct snapimage **snapimage_array;
#if defined(HAVE_SUPER_BLOCK_FREEZE)
	struct super_block **superblock_array;
#endif
};

void snapshot_done(void);

int snapshot_create(struct blk_snap_dev_t *dev_id_array, unsigned int count,
		    uuid_t *id);
int snapshot_destroy(uuid_t *id);
int snapshot_append_storage(uuid_t *id, struct blk_snap_dev_t dev_id,
			    struct big_buffer *ranges,
			    unsigned int range_count);
int snapshot_take(uuid_t *id);
struct event *snapshot_wait_event(uuid_t *id, unsigned long timeout_ms);
int snapshot_collect(unsigned int *pcount, uuid_t __user *id_array);
int snapshot_collect_images(uuid_t *id,
			    struct blk_snap_image_info __user *image_info_array,
			    unsigned int *pcount);
int snapshot_mark_dirty_blocks(dev_t image_dev_id,
			       struct blk_snap_block_range *block_ranges,
			       unsigned int count);

#ifdef BLK_SNAP_DEBUG_SECTOR_STATE
int snapshot_get_chunk_state(dev_t image_dev_id, sector_t sector,
			     struct blk_snap_sector_state *state);
#endif
