/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/uuid.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>

#include "big_buffer.h"

struct blk_snap_block_range;

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
	uuid_t generation_id;

	bool is_corrupted;

	sector_t state_changed_sectors;
	sector_t state_dirty_sectors;
};

struct cbt_map *cbt_map_create(struct block_device* bdev);
int cbt_map_reset(struct cbt_map *cbt_map, sector_t device_capacity);

void cbt_map_destroy_cb(struct kref *kref);
static inline
void cbt_map_get(struct cbt_map *cbt_map)
{
	kref_get(&cbt_map->kref);
};
static inline
void cbt_map_put(struct cbt_map *cbt_map)
{
	if (likely(cbt_map))
		kref_put(&cbt_map->kref, cbt_map_destroy_cb);
};

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
	return 1 << cbt_map->blk_size_shift;
};

int cbt_map_mark_dirty_blocks(struct cbt_map *cbt_map,
			      struct blk_snap_block_range *block_ranges,
			      unsigned int count);
