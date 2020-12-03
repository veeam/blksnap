/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "cow_blk.h"
/*
struct cow_map {
	struct rw_semaphore lock;
	struct kref kref; // defer_io can be shared between tracker and snapimage
	struct xarray blocks;

	dev_t dev_id;
	sector_t dev_capacity;
	bool is_corrupted;
};

static inline bool cow_is_corrupted(struct cow_map* cow_map)
{
	return cow_map->is_corrupted;
}

static inline void cow_corrupted(struct cow_map* cow_map)
{
	cow_map->is_corrupted = true;
	pr_err("Snapshot for device %d:%d was corrupted.\n", MAJOR(cow_map->dev_id), MINOR(cow_map->dev_id));
}

int cow_do(struct cow_map* cow_map, struct block_device *bdev, sector_t start, sector_t cnt);
*/
