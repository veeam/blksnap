// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-diff-storage"
#include "common.h"
#include "chunk.h"
#include "diff_storage.h"

struct storage_bdev
{
	struct list_head link;
	dev_t dev_id;
	struct block_device *bdev;
}

struct storage_block
{
	struct list_head link;
	struct block_device *bdev;
	sector_t sector;
	sector_t count;
	sector_t used;
}


struct diff_store *diff_store_new(struct block_device *bdev, sector_t sector, sector_t count)
{
	struct diff_store *store;

	store = kzalloc(sizeof(struct diff_store), GFP_NOIO);
	if (!store)
		return NULL;

	store->bdev = bdev;
	store->sector = sector;
	store->count = count;
}

void diff_store_free(struct diff_store *diff_store)
{
	kfree(store);
}


struct diff_storage *diff_storage_new(void)
{
	struct diff_storage *diff_storage;

	diff_storage = kzalloc(sizeof(struct diff_storage), GFP_KERNEL);
	if (!diff_storage)
		return NULL;

	spin_lock_init(&diff_storage->lock);
	INIT_LIST_HEAD(&diff_storage->bdevs);
	INIT_LIST_HEAD(&diff_storage->empty_blocks);
	INIT_LIST_HEAD(&diff_storage->filled_blocks);
	return diff_storage;
}

void diff_storage_free(struct kref *kref)
{
	struct diff_storage *diff_storage = container_of(kref, struct diff_storage, kref);
	struct storage_block *storage_block;
	struct storage_bdev *storage_bdev;

	while((storage_block = list_first_entry_or_null(&diff_storage->storage_empty_blocks, struct storage_block, link))) {
		list_del(storage_block);
		kfree(storage_block);
	}

	while((storage_block = list_first_entry_or_null(&diff_storage->storage_filled_blocks, struct storage_block, link))) {
		list_del(storage_block);
		kfree(storage_block);
	}

	while((storage_bdev = list_first_entry_or_null(&diff_storage->storage_bdevs, struct storage_bdev, link))) {
		blkdev_put(storage_bdev->bdev);
		list_del(storage_bdev);
		kfree(storage_bdev);
	}
}


struct block_device *diff_storage_bdev_by_id(struct diff_storage *diff_storage, dev_t dev_id)
{
	struct block-device *bdev = NULL;
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

int diff_storage_append_block(struct diff_storage *diff_storage, dev_t dev_id,
                              sector_t sector, sector_t count)
{
	struct storage_block *storage_block;
	struct block_device *bdev;

	bdev = diff_storage_bdev_by_id(diff_storage, dev_id);
	if (!bdev) {
		struct storage_bdev *storage_bdev;

		bdev = blkdev_get_by_dev(dev_id, 0, NULL);
		if (IS_ERR(bdev)) {
			pr_err("Failed to open device. errno=%ld\n", PTR_ERR(bdev));
			return PTR_ERR(bdev);
		}

		storage_bdev = kzalloc(sizeof(struct storage_bdev), GFP_KERNEL);
		if (!storage_bdev) {
			blkdev_put(bdev);
			return -NOMEM;
		}

		storage_bdev->bdev = bdev;
		storage_bdev->dev_id = dev_id;
		INIT_LIST_HEAD(&storage_bdev->link);

		spin_lock(&diff_storage->lock);
		list_add_tail(&storage_bdev->link, &diff_storage->storage_bdevs);
		spin_unlock(&diff_storage->lock);
	}

	storage_block = kzalloc(sizeof(struct storage_block), GFP_KERNEL);
	if (!storage_block)
		return -ENOMEM;

	INIT_LIST_HEAD(&storage_block->link);
	storage_block->bdev = bdev;
	storage_block->sector = sector;
	storage_block->count = count;
	

	spin_lock(&diff_storage->lock);
	list_add_tail(&storage_block->link, &diff_storage->storage_empty_blocks);
	diff_storage->empty_count += count;
	spin_unlock(&diff_storage->lock);
}

struct diff_store *diff_storage_get_store(struct diff_storage *diff_storage, sector_t count)
{
	int ret = 0;
	struct diff_store *diff_store;
	struct storage_block *storage_block;

	diff_store = kzalloc(sizeof(struct diff_store), GFP_KERNEL);
	if (!diff_store)
		return ERR_PTR(-ENOMEM);

	spin_lock(&diff_storage->lock);
	do {
		storage_block = list_first_entry_or_null(&diff_storage->storage_empty_blocks, struct storage_block, link);
		if (!storage_block) {
			ret = -ENOSPC;
			break;
		}

		if ((storage_block->count - storage_block->used) >= count) {
			diff_store.bdev = storage_block.bdev;
			diff_store.sector = storage_block.sector + storage_block->used;
			diff_store.count = count;

			storage_block->used += count;
			diff_storage->filled_count += count;
			break;
		}
		list_del(&storage_block->link);
		list_add_tail(&storage_block->link, &diff_storage->storage_filled_blocks);
		/**
		 * If there is still free space in the storage block, but
		 * it is not enough to store a piece, then such a block is
		 * considered used.
		 * We believe that the storage blocks are large enough
		 * to accommodate several pieces entirely.
		 */
		diff_storage->filled_count += (storage_block->count - storage_block->used);
	} while (1);
	spin_unlock(&diff_storage->lock);

	if (ret) {
		kfree(diff_store);
		return ERR_PTR(ret);
	}

	return diff_store;
}
