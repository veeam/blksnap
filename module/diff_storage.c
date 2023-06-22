// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME "-diff-storage: " fmt
#include <linux/slab.h>
#include <linux/sched/mm.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#ifdef STANDALONE_BDEVFILTER
#include "blk_snap.h"
#else
#include <linux/blk_snap.h>
#endif
#include "memory_checker.h"
#include "params.h"
#include "chunk.h"
#include "diff_io.h"
#include "diff_buffer.h"
#include "diff_storage.h"
#ifdef STANDALONE_BDEVFILTER
#include "log.h"
#endif

#ifndef PAGE_SECTORS
#define PAGE_SECTORS	(1 << (PAGE_SHIFT - SECTOR_SHIFT))
#endif

/**
 * struct storage_bdev - Information about the opened block device.
 */
struct storage_bdev {
	struct list_head link;
	dev_t dev_id;
	struct block_device *bdev;
};

/**
 * struct storage_block - A storage unit reserved for storing differences.
 *
 */
struct storage_block {
	struct list_head link;
	struct block_device *bdev;
	sector_t sector;
	sector_t count;
	sector_t used;
};

static inline void diff_storage_event_low(struct diff_storage *diff_storage)
{
	struct blk_snap_event_low_free_space data = {
		.requested_nr_sect = diff_storage_minimum,
	};

	diff_storage->requested += data.requested_nr_sect;
	pr_debug("Diff storage low free space. Portion: %llu sectors, requested: %llu\n",
		data.requested_nr_sect, diff_storage->requested);
	event_gen(&diff_storage->event_queue, GFP_NOIO,
		  blk_snap_event_code_low_free_space, &data, sizeof(data));
}

struct diff_storage *diff_storage_new(void)
{
	struct diff_storage *diff_storage;

	diff_storage = kzalloc(sizeof(struct diff_storage), GFP_KERNEL);
	if (!diff_storage)
		return NULL;
	memory_object_inc(memory_object_diff_storage);

	kref_init(&diff_storage->kref);
	spin_lock_init(&diff_storage->lock);
	INIT_LIST_HEAD(&diff_storage->storage_bdevs);
	INIT_LIST_HEAD(&diff_storage->empty_blocks);
	INIT_LIST_HEAD(&diff_storage->filled_blocks);

	event_queue_init(&diff_storage->event_queue);
	diff_storage_event_low(diff_storage);

	return diff_storage;
}

static inline struct storage_block *
first_empty_storage_block(struct diff_storage *diff_storage)
{
	return list_first_entry_or_null(&diff_storage->empty_blocks,
					struct storage_block, link);
};

static inline struct storage_block *
first_filled_storage_block(struct diff_storage *diff_storage)
{
	return list_first_entry_or_null(&diff_storage->filled_blocks,
					struct storage_block, link);
};

static inline struct storage_bdev *
first_storage_bdev(struct diff_storage *diff_storage)
{
	return list_first_entry_or_null(&diff_storage->storage_bdevs,
					struct storage_bdev, link);
};

void diff_storage_free(struct kref *kref)
{
	struct diff_storage *diff_storage =
		container_of(kref, struct diff_storage, kref);
	struct storage_block *blk;
	struct storage_bdev *storage_bdev;

	while ((blk = first_empty_storage_block(diff_storage))) {
		list_del(&blk->link);
		kfree(blk);
		memory_object_dec(memory_object_storage_block);
	}

	while ((blk = first_filled_storage_block(diff_storage))) {
		list_del(&blk->link);
		kfree(blk);
		memory_object_dec(memory_object_storage_block);
	}

	while ((storage_bdev = first_storage_bdev(diff_storage))) {
		blkdev_put(storage_bdev->bdev, FMODE_READ | FMODE_WRITE);
		list_del(&storage_bdev->link);
		kfree(storage_bdev);
		memory_object_dec(memory_object_storage_bdev);
	}
	event_queue_done(&diff_storage->event_queue);

	kfree(diff_storage);
	memory_object_dec(memory_object_diff_storage);
}

static struct block_device *
diff_storage_bdev_by_id(struct diff_storage *diff_storage, dev_t dev_id)
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

static inline struct block_device *
diff_storage_add_storage_bdev(struct diff_storage *diff_storage, dev_t dev_id)
{
	struct block_device *bdev;
	struct storage_bdev *storage_bdev;

#if defined(HAVE_BLK_HOLDER_OPS)
	bdev = blkdev_get_by_dev(dev_id, FMODE_READ | FMODE_WRITE, NULL, NULL);
#else
	bdev = blkdev_get_by_dev(dev_id, FMODE_READ | FMODE_WRITE, NULL);
#endif
	if (IS_ERR(bdev)) {
		pr_err("Failed to open device. errno=%d\n",
		       abs((int)PTR_ERR(bdev)));
		return bdev;
	}

	storage_bdev = kzalloc(sizeof(struct storage_bdev), GFP_KERNEL);
	if (!storage_bdev) {
		blkdev_put(bdev, FMODE_READ | FMODE_WRITE);
		return ERR_PTR(-ENOMEM);
	}
	memory_object_inc(memory_object_storage_bdev);

	storage_bdev->bdev = bdev;
	storage_bdev->dev_id = dev_id;
	INIT_LIST_HEAD(&storage_bdev->link);

	spin_lock(&diff_storage->lock);
	list_add_tail(&storage_bdev->link, &diff_storage->storage_bdevs);
	spin_unlock(&diff_storage->lock);

	return bdev;
}

static inline int diff_storage_add_range(struct diff_storage *diff_storage,
					 struct block_device *bdev,
					 sector_t sector, sector_t count)
{
	struct storage_block *storage_block;

	pr_debug("Add range to diff storage: [%u:%u] %llu:%llu\n",
		 MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev), sector, count);

	storage_block = kzalloc(sizeof(struct storage_block), GFP_KERNEL);
	if (!storage_block)
		return -ENOMEM;
	memory_object_inc(memory_object_storage_block);

	INIT_LIST_HEAD(&storage_block->link);
	storage_block->bdev = bdev;
	storage_block->sector = sector;
	storage_block->count = count;

	spin_lock(&diff_storage->lock);
	list_add_tail(&storage_block->link, &diff_storage->empty_blocks);
#ifdef BLK_SNAP_DEBUG_DIFF_STORAGE_LISTS
	atomic_inc(&diff_storage->free_block_count);
#endif
	diff_storage->capacity += count;
	spin_unlock(&diff_storage->lock);
#ifdef BLK_SNAP_DEBUG_DIFF_STORAGE_LISTS
	pr_debug("free storage blocks %d\n",
		 atomic_read(&diff_storage->free_block_count));
#endif

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

	pr_debug("Append %u blocks\n", range_count);

	bdev = diff_storage_bdev_by_id(diff_storage, dev_id);
	if (!bdev) {
		bdev = diff_storage_add_storage_bdev(diff_storage, dev_id);
		if (IS_ERR(bdev))
			return PTR_ERR(bdev);
	}

	for (inx = 0; inx < range_count; inx++) {
		range = big_buffer_get_element(
			ranges, inx, sizeof(struct blk_snap_block_range));
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

static inline bool is_halffull(const sector_t sectors_left)
{
	return sectors_left <= ((diff_storage_minimum >> 1) & ~(PAGE_SECTORS - 1));
}

struct diff_region *diff_storage_new_region(struct diff_storage *diff_storage,
					   sector_t count)
{
	int ret = 0;
	struct diff_region *diff_region;
	sector_t sectors_left;

	if (atomic_read(&diff_storage->overflow_flag))
		return ERR_PTR(-ENOSPC);

	diff_region = kzalloc(sizeof(struct diff_region), GFP_NOIO);
	if (!diff_region)
		return ERR_PTR(-ENOMEM);
	memory_object_inc(memory_object_diff_region);

	spin_lock(&diff_storage->lock);
	do {
		struct storage_block *storage_block;
		sector_t available;

		storage_block = first_empty_storage_block(diff_storage);
		if (unlikely(!storage_block)) {
			atomic_inc(&diff_storage->overflow_flag);
			ret = -ENOSPC;
			break;
		}

		available = storage_block->count - storage_block->used;
		if (likely(available >= count)) {
			diff_region->bdev = storage_block->bdev;
			diff_region->sector =
				storage_block->sector + storage_block->used;
			diff_region->count = count;

			storage_block->used += count;
			diff_storage->filled += count;
			break;
		}

		list_del(&storage_block->link);
		list_add_tail(&storage_block->link,
			      &diff_storage->filled_blocks);
#ifdef BLK_SNAP_DEBUG_DIFF_STORAGE_LISTS
		atomic_dec(&diff_storage->free_block_count);
		atomic_inc(&diff_storage->user_block_count);
#endif
		/*
		 * If there is still free space in the storage block, but
		 * it is not enough to store a piece, then such a block is
		 * considered used.
		 * We believe that the storage blocks are large enough
		 * to accommodate several pieces entirely.
		 */
		diff_storage->filled += available;
	} while (1);
	sectors_left = diff_storage->requested - diff_storage->filled;
	spin_unlock(&diff_storage->lock);

#ifdef BLK_SNAP_DEBUG_DIFF_STORAGE_LISTS
	pr_debug("free storage blocks %d\n",
		 atomic_read(&diff_storage->free_block_count));
	pr_debug("user storage blocks %d\n",
		 atomic_read(&diff_storage->user_block_count));
#endif

	if (ret) {
		pr_err("Cannot get empty storage block\n");
		diff_storage_free_region(diff_region);
		return ERR_PTR(ret);
	}

	if (is_halffull(sectors_left) &&
	    (atomic_inc_return(&diff_storage->low_space_flag) == 1))
		diff_storage_event_low(diff_storage);

	return diff_region;
}
