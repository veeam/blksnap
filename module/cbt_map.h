/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/kref.h>
#include <linux/uuid.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>

#include "big_buffer.h"

/**
 * struct cbt_map - The table of changes fo block device.
 * 
 */
struct cbt_map {
	struct kref kref;

	spinlock_t locker;

	size_t blk_size_shift;
	size_t blk_count;
	sector_t device_capacity;

	struct big_buffer *read_map;
	struct big_buffer *write_map;

	unsigned long snap_number_active;
	unsigned long snap_number_previous;
	uuid_t generationId;

	bool is_corrupted;

	sector_t state_changed_sectors;
	sector_t state_dirty_sectors;
};

struct cbt_map *cbt_map_create(struct block_device* bdev);
int cbt_map_reset(struct cbt_map *cbt_map, sector_t device_capacity);

struct cbt_map *cbt_map_get_resource(struct cbt_map *cbt_map);
void cbt_map_put_resource(struct cbt_map *cbt_map);

void cbt_map_switch(struct cbt_map *cbt_map);
int cbt_map_set(struct cbt_map *cbt_map,
                sector_t sector_start, sector_t sector_cnt);
int cbt_map_set_both(struct cbt_map *cbt_map,
                     sector_t sector_start, sector_t sector_cnt);

size_t cbt_map_read_to_user(struct cbt_map *cbt_map, char __user *user_buffer,
                            size_t offset, size_t size);

static inline
size_t cbt_map_blk_size(struct cbt_map *cbt_map)
{
	return 1 << (cbt_map->blk_size_shift + SECTOR_SHIFT);
};

#ifndef HAVE_BDEV_NR_SECTORS
static inline
sector_t bdev_nr_sectors(struct block_device *bdev)
{
	return i_size_read(bdev->bd_inode) >> 9;
};
#endif
