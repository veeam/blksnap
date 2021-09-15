// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME "-diff-storage: " fmt
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/spinlock.h>

#include "blk_snap.h"
#include "params.h"
#include "chunk.h"
#include "diff_storage.h"

/**
 * struct storage_bdev - Information about opened block device.
 */
struct storage_bdev
{
	struct list_head link;
	dev_t dev_id;
	struct block_device *bdev;
};

/**
 * struct storage_block - A storage unit reserved for storing differences.
 * 
 */
struct storage_block
{
	struct list_head link;
	struct block_device *bdev;
	sector_t sector;
	sector_t count;
	sector_t used;
};

struct diff_storage *diff_storage_new(void)
{
	struct diff_storage *diff_storage;

	diff_storage = kzalloc(sizeof(struct diff_storage), GFP_KERNEL);
	if (!diff_storage)
		return NULL;

	spin_lock_init(&diff_storage->lock);
	INIT_LIST_HEAD(&diff_storage->storage_bdevs);
	INIT_LIST_HEAD(&diff_storage->empty_blocks);
	INIT_LIST_HEAD(&diff_storage->filled_blocks);

	event_queue_init(&diff_storage->event_queue);

	return diff_storage;
}

static inline
struct storage_block *first_empty_storage_block(struct diff_storage *diff_storage)
{
	return list_first_entry_or_null(&diff_storage->empty_blocks,
	                                struct storage_block, link);
};

static inline
struct storage_block *first_filled_storage_block(struct diff_storage *diff_storage)
{
	return list_first_entry_or_null(&diff_storage->filled_blocks,
	                                struct storage_block, link);
};

static inline 
struct storage_bdev *first_storage_bdev(struct diff_storage *diff_storage)
{
	return list_first_entry_or_null(&diff_storage->storage_bdevs,
	                                struct storage_bdev, link);
};

void diff_storage_free(struct kref *kref)
{
	struct diff_storage *diff_storage = container_of(kref, struct diff_storage, kref);
	struct storage_block *blk;
	struct storage_bdev *storage_bdev;

	while ((blk = first_empty_storage_block(diff_storage))) {
		list_del(&blk->link);
		kfree(blk);
	}

	while ((blk = first_filled_storage_block(diff_storage))) {
		list_del(&blk->link);
		kfree(blk);
	}

	while ((storage_bdev = first_storage_bdev(diff_storage))) {
		blkdev_put(storage_bdev->bdev, FMODE_READ | FMODE_WRITE);
		list_del(&storage_bdev->link);
		kfree(storage_bdev);
	}

	event_queue_done(&diff_storage->event_queue);
}

struct block_device *diff_storage_bdev_by_id(struct diff_storage *diff_storage, dev_t dev_id)
{
	struct block_device *bdev = NULL;
	struct storage_bdev *storage_bdev;

	spin_lock(&diff_storage->lock);
	list_for_each_entry(storage_bdev, &diff_storage->storage_bdevs, link) {
		if (storage_bdev->dev_id == dev_id) {
			bdev = storage_bdev->bdev;
			break;
		}
	}
	spin_unlock(&diff_storage->lock);

	return bdev;
}

static inline 
struct block_device *diff_storage_add_storage_bdev(struct diff_storage *diff_storage,
						   dev_t dev_id)
{
	struct block_device *bdev;
	struct storage_bdev *storage_bdev;

	bdev = blkdev_get_by_dev(dev_id, FMODE_READ | FMODE_WRITE, NULL);
	if (IS_ERR(bdev)) {
		pr_err("Failed to open device. errno=%ld\n", PTR_ERR(bdev));
		return bdev;
	}

	storage_bdev = kzalloc(sizeof(struct storage_bdev), GFP_KERNEL);
	if (!storage_bdev) {
		blkdev_put(bdev, FMODE_READ | FMODE_WRITE);
		return ERR_PTR(-ENOMEM);
	}

	storage_bdev->bdev = bdev;
	storage_bdev->dev_id = dev_id;
	INIT_LIST_HEAD(&storage_bdev->link);

	spin_lock(&diff_storage->lock);
	list_add_tail(&storage_bdev->link, &diff_storage->storage_bdevs);
	spin_unlock(&diff_storage->lock);	

	return bdev;
}

static inline 
int diff_storage_add_range(struct diff_storage *diff_storage,
                           struct block_device *bdev,
                           sector_t sector, sector_t count)
{
	struct storage_block *storage_block;

	storage_block = kzalloc(sizeof(struct storage_block), GFP_KERNEL);
	if (!storage_block)
		return -ENOMEM;

	INIT_LIST_HEAD(&storage_block->link);
	storage_block->bdev = bdev;
	storage_block->sector = sector;
	storage_block->count = count;

	spin_lock(&diff_storage->lock);
	list_add_tail(&storage_block->link, &diff_storage->empty_blocks);
	diff_storage->capacity += count;
	spin_unlock(&diff_storage->lock);

	return 0;
}

int diff_storage_append_block(struct diff_storage *diff_storage, dev_t dev_id,
                              struct big_buffer *ranges,
                              unsigned int range_count)
{
	int ret;
	int inx;
	struct block_device *bdev;
	struct blk_snap_block_range *range;

	bdev = diff_storage_bdev_by_id(diff_storage, dev_id);
	if (!bdev) {
		bdev = diff_storage_add_storage_bdev(diff_storage, dev_id);
		if (IS_ERR(bdev))
			return PTR_ERR(bdev);
	}

	for (inx = 0; inx < range_count; inx++) {
		range = big_buffer_get_element(ranges, inx,
					sizeof(struct blk_snap_block_range));
		if (unlikely(!range))
			return -EINVAL;

		ret = diff_storage_add_range(diff_storage, bdev,
		                             range->sector_offset,
		                             range->sector_count);
		if (unlikely(ret))
			return ret;
	}

	if (atomic_read(&diff_storage->low_space_flag) &&
	    (diff_storage->capacity >= diff_storage->requested))
		atomic_set(&diff_storage->low_space_flag, 0);

	return 0;
}

struct diff_store *diff_storage_get_store(struct diff_storage *diff_storage, sector_t count)
{
	int ret = 0;
	struct diff_store *diff_store;
	struct storage_block *storage_block;
	sector_t sectors_left;

	if (atomic_read(&diff_storage->overflow_flag))
		return ERR_PTR(-ENOSPC);

	diff_store = kzalloc(sizeof(struct diff_store), GFP_KERNEL);
	if (!diff_store)
		return ERR_PTR(-ENOMEM);

	spin_lock(&diff_storage->lock);
	do {
		storage_block = first_empty_storage_block(diff_storage);
		if (!storage_block) {
			/*
			 *
			 *
			 */
			atomic_inc(&diff_storage->overflow_flag);
			ret = -ENOSPC;
			break;
		}

		if ((storage_block->count - storage_block->used) >= count) {
			diff_store->bdev = storage_block->bdev;
			diff_store->sector = storage_block->sector + storage_block->used;
			diff_store->count = count;

			storage_block->used += count;
			diff_storage->filled += count;
			break;
		}
		list_del(&storage_block->link);
		list_add_tail(&storage_block->link, &diff_storage->filled_blocks);
		/*
		 * If there is still free space in the storage block, but
		 * it is not enough to store a piece, then such a block is
		 * considered used.
		 * We believe that the storage blocks are large enough
		 * to accommodate several pieces entirely.
		 */
		diff_storage->filled += (storage_block->count - storage_block->used);
	} while (1);
	sectors_left = diff_storage->requested - diff_storage->filled;
	spin_unlock(&diff_storage->lock);

	if (ret) {
		kfree(diff_store);
		return ERR_PTR(ret);
	}

	if ((sectors_left <= (diff_storage_minimum >> SECTOR_SHIFT)) &&
	    (atomic_inc_return(&diff_storage->low_space_flag) == 1)) {
		struct blk_snap_event_low_free_space data = {
			.requested_nr_sect = diff_storage_minimum >> SECTOR_SHIFT
		};

		diff_storage->requested += data.requested_nr_sect;
		event_gen(&diff_storage->event_queue, GFP_NOIO, 
			BLK_SNAP_EVENT_LOW_FREE_SPACE,
			&data, sizeof(data));
	}

	return diff_store;
}
