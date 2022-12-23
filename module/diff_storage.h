/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BLK_SNAP_DIFF_STORAGE_H
#define __BLK_SNAP_DIFF_STORAGE_H

#include "event_queue.h"

struct blk_snap_block_range;
struct diff_region;

/**
 * struct diff_storage - Difference storage.
 *
 * @kref:
 *	The reference counter.
 * @lock:
 *	Spinlock allows to guarantee the safety of linked lists.
 * @storage_bdevs:
 *	List of opened block devices. Blocks for storing snapshot data can be
 *	located on different block devices. So, all opened block devices are
 *	located in this list. Blocks on opened block devices are allocated for
 *	storing the chunks data.
 * @empty_blocks:
 *	List of empty blocks on storage. This list can be updated while
 *	holding a snapshot. This allows us to dynamically increase the
 *	storage size for these snapshots.
 * @filled_blocks:
 *	List of filled blocks. When the blocks from the list of empty blocks are filled,
 *	we move them to the list of filled blocks.
 * @capacity:
 *	Total amount of available storage space.
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
 * @event_queue:
 *	A queue of events to pass events to user space. Diff storage and its
 *	owner can notify its snapshot about events like snapshot overflow,
 *	low free space and snapshot terminated.
 *
 * The difference storage manages the regions of block devices that are used
 * to store the data of the original block devices in the snapshot.
 * The difference storage is created one per snapshot and is used to store
 * data from all the original snapshot block devices. At the same time, the
 * difference storage itself can contain regions on various block devices.
 */
struct diff_storage {
	struct kref kref;
	spinlock_t lock;

	struct list_head storage_bdevs;
	struct list_head empty_blocks;
	struct list_head filled_blocks;

	sector_t capacity;
	sector_t filled;
	sector_t requested;

	atomic_t low_space_flag;
	atomic_t overflow_flag;

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

int diff_storage_append_block(struct diff_storage *diff_storage, dev_t dev_id,
			      struct blk_snap_block_range __user *ranges,
			      unsigned int range_count);
struct diff_region *diff_storage_new_region(struct diff_storage *diff_storage,
					    sector_t count);

static inline void diff_storage_free_region(struct diff_region *region)
{
	kfree(region);
	if (region)
		memory_object_dec(memory_object_diff_region);
}
#endif /* __BLK_SNAP_DIFF_STORAGE_H */
