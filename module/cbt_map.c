// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Veeam Software Group GmbH */
#define pr_fmt(fmt) KBUILD_MODNAME "-cbt_map: " fmt

#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <uapi/linux/blksnap.h>
#include "cbt_map.h"
#include "params.h"

static inline unsigned long long count_by_shift(sector_t capacity,
						unsigned long long shift)
{
	sector_t blk_size = 1ull << (shift - SECTOR_SHIFT);

	return round_up(capacity, blk_size) / blk_size;
}

static void cbt_map_calculate_block_size(struct cbt_map *cbt_map)
{
	unsigned long long count;
	unsigned long long shift = get_tracking_block_minimum_shift();

	pr_debug("Device capacity %llu sectors\n", cbt_map->device_capacity);
	/*
	 * The size of the tracking block is calculated based on the size of
	 * the disk so that the CBT table does not exceed a reasonable size.
	 */
	count = count_by_shift(cbt_map->device_capacity, shift);
	pr_debug("Blocks count %llu\n", count);
	while (count > get_tracking_block_maximum_count()) {
		if (shift >= get_tracking_block_maximum_shift()) {
			pr_info("The maximum allowable CBT block size has been reached.\n");
			break;
		}
		shift = shift + 1ull;
		count = count_by_shift(cbt_map->device_capacity, shift);
		pr_debug("Blocks count %llu\n", count);
	}

	cbt_map->blk_size_shift = shift;
	cbt_map->blk_count = count;
	pr_debug("The optimal CBT block size was calculated as %llu bytes\n",
		 (1ull << cbt_map->blk_size_shift));
}

static int cbt_map_allocate(struct cbt_map *cbt_map)
{
	unsigned char *read_map = NULL;
	unsigned char *write_map = NULL;
	size_t size = cbt_map->blk_count;

	pr_debug("Allocate CBT map of %zu blocks\n", size);

	if (cbt_map->read_map || cbt_map->write_map)
		return -EINVAL;

	read_map = __vmalloc(size, GFP_NOIO | __GFP_ZERO);
	if (!read_map)
		return -ENOMEM;

	write_map = __vmalloc(size, GFP_NOIO | __GFP_ZERO);
	if (!write_map) {
		vfree(read_map);
		return -ENOMEM;
	}

	cbt_map->read_map = read_map;
	cbt_map->write_map = write_map;

	cbt_map->snap_number_previous = 0;
	cbt_map->snap_number_active = 1;
	generate_random_uuid(cbt_map->generation_id.b);
	cbt_map->is_corrupted = false;

	return 0;
}

static void cbt_map_deallocate(struct cbt_map *cbt_map)
{
	cbt_map->is_corrupted = false;

	if (cbt_map->read_map) {
		vfree(cbt_map->read_map);
		cbt_map->read_map = NULL;
	}

	if (cbt_map->write_map) {
		vfree(cbt_map->write_map);
		cbt_map->write_map = NULL;
	}
}

int cbt_map_reset(struct cbt_map *cbt_map, sector_t device_capacity)
{
	cbt_map_deallocate(cbt_map);

	cbt_map->device_capacity = device_capacity;
	cbt_map_calculate_block_size(cbt_map);

	return cbt_map_allocate(cbt_map);
}

void cbt_map_destroy(struct cbt_map *cbt_map)
{
	pr_debug("CBT map destroy\n");

	cbt_map_deallocate(cbt_map);
	kfree(cbt_map);
}

struct cbt_map *cbt_map_create(struct block_device *bdev)
{
	struct cbt_map *cbt_map = NULL;
	int ret;

	pr_debug("CBT map create\n");

	cbt_map = kzalloc(sizeof(struct cbt_map), GFP_KERNEL);
	if (cbt_map == NULL)
		return NULL;

	cbt_map->device_capacity = bdev_nr_sectors(bdev);
	cbt_map_calculate_block_size(cbt_map);

	ret = cbt_map_allocate(cbt_map);
	if (ret) {
		pr_err("Failed to create tracker. errno=%d\n", abs(ret));
		cbt_map_destroy(cbt_map);
		return NULL;
	}

	spin_lock_init(&cbt_map->locker);
	cbt_map->is_corrupted = false;

	return cbt_map;
}

void cbt_map_switch(struct cbt_map *cbt_map)
{
	pr_debug("CBT map switch\n");
	spin_lock(&cbt_map->locker);

	cbt_map->snap_number_previous = cbt_map->snap_number_active;
	++cbt_map->snap_number_active;
	if (cbt_map->snap_number_active == 256) {
		cbt_map->snap_number_active = 1;

		memset(cbt_map->write_map, 0, cbt_map->blk_count);

		generate_random_uuid(cbt_map->generation_id.b);

		pr_debug("CBT reset\n");
	} else
		memcpy(cbt_map->read_map, cbt_map->write_map,
		       cbt_map->blk_count);
	spin_unlock(&cbt_map->locker);
}

static inline int _cbt_map_set(struct cbt_map *cbt_map, sector_t sector_start,
			       sector_t sector_cnt, u8 snap_number,
			       unsigned char *map)
{
	int res = 0;
	u8 num;
	size_t inx;
	size_t cbt_block_first = (size_t)(
		sector_start >> (cbt_map->blk_size_shift - SECTOR_SHIFT));
	size_t cbt_block_last = (size_t)(
		(sector_start + sector_cnt - 1) >>
		(cbt_map->blk_size_shift - SECTOR_SHIFT));

	for (inx = cbt_block_first; inx <= cbt_block_last; ++inx) {
		if (unlikely(inx >= cbt_map->blk_count)) {
			pr_err("Block index is too large\n");
			pr_err("Block #%zu was demanded, map size %zu blocks\n",
			       inx, cbt_map->blk_count);
			res = -EINVAL;
			break;
		}

		num = map[inx];
		if (num < snap_number)
			map[inx] = snap_number;
	}
	return res;
}

int cbt_map_set(struct cbt_map *cbt_map, sector_t sector_start,
		sector_t sector_cnt)
{
	int res;

	spin_lock(&cbt_map->locker);
	if (unlikely(cbt_map->is_corrupted)) {
		spin_unlock(&cbt_map->locker);
		return -EINVAL;
	}
	res = _cbt_map_set(cbt_map, sector_start, sector_cnt,
			   (u8)cbt_map->snap_number_active, cbt_map->write_map);
	if (unlikely(res))
		cbt_map->is_corrupted = true;

	spin_unlock(&cbt_map->locker);

	return res;
}

int cbt_map_set_both(struct cbt_map *cbt_map, sector_t sector_start,
		     sector_t sector_cnt)
{
	int res;

	spin_lock(&cbt_map->locker);
	if (unlikely(cbt_map->is_corrupted)) {
		spin_unlock(&cbt_map->locker);
		return -EINVAL;
	}
	res = _cbt_map_set(cbt_map, sector_start, sector_cnt,
			   (u8)cbt_map->snap_number_active, cbt_map->write_map);
	if (!res)
		res = _cbt_map_set(cbt_map, sector_start, sector_cnt,
				   (u8)cbt_map->snap_number_previous,
				   cbt_map->read_map);
	spin_unlock(&cbt_map->locker);

	return res;
}
