/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once


/**
 * struct diff_store - Difference store
 * Describes the location of the chunk on difference storage.
 */
struct diff_store {
	struct list_head link;
	struct block_device *bdev;
	sector_t sector;
	sector_t count;
};

struct diff_store *diff_store_new(struct block_device *bdev, sector_t sector, sector_t count);
void diff_store_free(struct diff_store *diff_store);



struct diff_storage
{
	struct kref kref;

	spinlock_t lock;
	/**
	 * bdevs - list of opened block devices
	 * Blocks for storing snapshot data can be located on different block devices.
	 * So, all opened block devices are located in this list.
	 */
	struct list_head storage_bdevs;
	/**
	 * empty_blocks - list of empty blocks on storage
	 * This list can be updated while holding a snapshot. It's allowing us
	 * to dynamically increase the storage size for these snapshots.
	 */
	struct list_head storage_empty_blocks;
	/**
	 * filled_blocks - list of filled blocks
	 * When the blocks from the empty list are filled, we move them to
	 * the filled list.
	 */
	struct list_head storage_filled_blocks;

	sector_t empty_count;
	sector_t filled_count;
}


struct diff_storage *diff_storage_new(void);
void diff_storage_free(struct kref *kref);

void diff_storage_get(struct diff_storage *diff_storage)
{
	kref_get(diff_storage->kref);
};
void diff_storage_put(struct diff_storage *diff_storage)
{
	kref_put(diff_storage->kref, diff_storage_free);
};


int diff_storage_append_block(struct diff_storage *diff_storage, dev_t dev_id,
                              sector_t sector, sector_t count);
struct diff_store *diff_storage_get_store(struct diff_storage *diff_storage,
                                          sector_t count);
