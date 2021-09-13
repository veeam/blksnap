/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "event_queue.h"
#include "big_buffer.h"

/**
 * struct diff_store - Describes the location of the chunks data on
 * 	difference storage.
 * 
 */
struct diff_store {
	struct list_head link;
	struct block_device *bdev;
	sector_t sector;
	sector_t count;
};

/**
 * struct diff_storage - Difference storage.
 * 
 * @kref:
 * 
 * @lock:
 * 
 * @storage_bdevs:
 * 	List of opened block devices. Blocks for storing snapshot data can be
 * 	located on different block devices.
 * 	So, all opened block devices are located in this list. 
 *	A storage from which blocks are allocated for storing chunks data.
 * @empty_blocks:
 * 	List of empty blocks on storage. This list can be updated while
 * 	holding a snapshot. It's allowing us to dynamically increase the
 * 	storage size for these snapshots.
 * @filled_blocks:
 * 	List of filled blocks. When the blocks from the empty list are filled,
 * 	we move them to the filled list.
 * @capacity:
 * 	Total amount of available storage space.
 * @filled:
 * 	The number of sectors already filled in.
 * @requested:
 * 	The number of sectors already requested from user-space.
 * @low_space_flag:
 * 
 * @overflow_flag:
 * 
 * @event_queue:
 * 	A queue of events to pass them to the user-space. Diff storage and his
 * 	owner can notify his snapshot about events like snapshot overflow,
 * 	low free space and snapshot terminated.
 */
struct diff_storage
{
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

static inline
void diff_storage_get(struct diff_storage *diff_storage)
{
	kref_get(&diff_storage->kref);
};
static inline
void diff_storage_put(struct diff_storage *diff_storage)
{
	if (likely(diff_storage))
		kref_put(&diff_storage->kref, diff_storage_free);
};

int diff_storage_append_block(struct diff_storage *diff_storage, dev_t dev_id,
                              struct big_buffer *ranges,
                              unsigned int range_count);
struct diff_store *diff_storage_get_store(struct diff_storage *diff_storage,
                                          sector_t count);
