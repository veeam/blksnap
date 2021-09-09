// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME "-cbt_map" ": " fmt

#include "cbt_map.h"
#include "params.h"

static inline 
unsigned long long count_by_shift(sector_t capacity, unsigned long long shift)
{
	return round_up(capacity,
	                1ull << (shift - SECTOR_SHIFT)) >> (shift - SECTOR_SHIFT);
}

static 
void cbt_map_calculate_block_size(struct cbt_map *cbt_map)
{
	unsigned long long shift;
	unsigned long long count;

	if (tracking_block_minimum_shift) {
		/**
		 * The tracking block size was set explicitly.
		 */
		shift = tracking_block_minimum_shift - SECTOR_SHIFT;
		count = count_by_shift(cbt_map->device_capacity, shift);
	} else {
		/**
		 * The size of the tracking block is calculated based on the size of the disk
		 * so that the CBT table does not exceed a reasonable size. 
		 */
		shift = tracking_block_minimum_shift;
		count = count_by_shift(cbt_map->device_capacity, shift);

		while (count > tracking_block_maximum_count) {
			shift = shift << 1;
			count = count_by_shift(cbt_map->device_capacity, shift);
		}
	}

	cbt_map->blk_size_shift = shift;
	cbt_map->blk_count = count;
}

static
int cbt_map_allocate(struct cbt_map *cbt_map)
{
	pr_info("Allocate CBT map of %zu\n", cbt_map->blk_count);

	cbt_map->read_map = big_buffer_alloc(cbt_map->blk_count, GFP_KERNEL);
	if (cbt_map->read_map != NULL)
		big_buffer_memset(cbt_map->read_map, 0);

	cbt_map->write_map = big_buffer_alloc(cbt_map->blk_count, GFP_KERNEL);
	if (cbt_map->write_map != NULL)
		big_buffer_memset(cbt_map->write_map, 0);

	if ((cbt_map->read_map == NULL) || (cbt_map->write_map == NULL)) {
		pr_err("Cannot allocate CBT map. blk_count=%zu\n", cbt_map->blk_count);
		return -ENOMEM;
	}

	cbt_map->snap_number_previous = 0;
	cbt_map->snap_number_active = 1;
	generate_random_uuid(cbt_map->generationId.b);
	cbt_map->is_corrupted = false;

	cbt_map->state_changed_sectors = 0;
	cbt_map->state_dirty_sectors = 0;

	return 0;
}

static
void cbt_map_deallocate(struct cbt_map *cbt_map)
{
	cbt_map->is_corrupted = false;

	if (cbt_map->read_map != NULL) {
		big_buffer_free(cbt_map->read_map);
		cbt_map->read_map = NULL;
	}

	if (cbt_map->write_map != NULL) {
		big_buffer_free(cbt_map->write_map);
		cbt_map->write_map = NULL;
	}
}

int cbt_map_reset(struct cbt_map *cbt_map, sector_t device_capacity)
{
	cbt_map_deallocate(cbt_map);

	cbt_map->device_capacity = device_capacity;
	cbt_map_calculate_block_size(cbt_map);
	cbt_map->is_corrupted = false;

	return cbt_map_allocate(cbt_map);
}

static
void cbt_map_destroy(struct cbt_map *cbt_map)
{
	pr_info("CBT map destroy\n");
	if (cbt_map != NULL) {
		cbt_map_deallocate(cbt_map);
		kfree(cbt_map);
	}
}

struct cbt_map *cbt_map_create(struct block_device* bdev)
{
	struct cbt_map *cbt_map = NULL;

	pr_info("CBT map create\n");

	cbt_map = kzalloc(sizeof(struct cbt_map), GFP_KERNEL);
	if (cbt_map == NULL)
		return NULL;

	cbt_map->device_capacity = bdev_nr_sectors(bdev);
	cbt_map_calculate_block_size(cbt_map);

	if (cbt_map_allocate(cbt_map)) {
		cbt_map_destroy(cbt_map);
		return NULL;
	}

	spin_lock_init(&cbt_map->locker);
	kref_init(&cbt_map->kref);
	cbt_map->is_corrupted = false;

	return cbt_map;
}

static
void _cbt_map_destroy_cb(struct kref *kref)
{
	cbt_map_destroy(container_of(kref, struct cbt_map, kref));
}

struct cbt_map *cbt_map_get_resource(struct cbt_map *cbt_map)
{
	if (cbt_map)
		kref_get(&cbt_map->kref);

	return cbt_map;
}

void cbt_map_put_resource(struct cbt_map *cbt_map)
{
	if (likely(cbt_map))
		kref_put(&cbt_map->kref, _cbt_map_destroy_cb);
}

void cbt_map_switch(struct cbt_map *cbt_map)
{
	pr_info("CBT map switch\n");
	spin_lock(&cbt_map->locker);

	big_buffer_memcpy(cbt_map->read_map, cbt_map->write_map);

	cbt_map->snap_number_previous = cbt_map->snap_number_active;
	++cbt_map->snap_number_active;
	if (cbt_map->snap_number_active == 256) {
		cbt_map->snap_number_active = 1;

		big_buffer_memset(cbt_map->write_map, 0);

		generate_random_uuid(cbt_map->generationId.b);

		pr_info("CBT reset\n");
	}
	spin_unlock(&cbt_map->locker);
}

static inline
int _cbt_map_set(struct cbt_map *cbt_map,
                 sector_t sector_start, sector_t sector_cnt,
		 u8 snap_number, struct big_buffer *map)
{
	int res = 0;
	u8 num;
	size_t cbt_block;
	size_t cbt_block_first = (size_t)(sector_start >> cbt_map->blk_size_shift);
	size_t cbt_block_last = (size_t)((sector_start + sector_cnt - 1) >>
					 cbt_map->blk_size_shift); //inclusive

	for (cbt_block = cbt_block_first; cbt_block <= cbt_block_last; ++cbt_block) {
		if (unlikely(cbt_block >= cbt_map->blk_count)) {
			pr_err("Block index is too large. #%zu was demanded, map size %zu\n",
			       cbt_block, cbt_map->blk_count);
			res = -EINVAL;
			break;
		}

		res = big_buffer_byte_get(map, cbt_block, &num);
		if (unlikely(res)) {
			pr_err("CBT table out of range\n");
			break;
		}

		if (num < snap_number) {
			res = big_buffer_byte_set(map, cbt_block, snap_number);
			if (unlikely(res)) {
				pr_err("CBT table out of range\n");
				break;
			}
		}
	}
	return res;
}

int cbt_map_set(struct cbt_map *cbt_map,
                sector_t sector_start, sector_t sector_cnt)
{
	int res;

	spin_lock(&cbt_map->locker);
	if (unlikely(cbt_map->is_corrupted)) {
		spin_unlock(&cbt_map->locker);
		return -EINVAL;
	}
	res = _cbt_map_set(cbt_map, sector_start, sector_cnt,
			   (u8)cbt_map->snap_number_active, cbt_map->write_map);
	if (res)
		cbt_map->is_corrupted = true;
	else
		cbt_map->state_changed_sectors += sector_cnt;
	spin_unlock(&cbt_map->locker);

	return res;
}

int cbt_map_set_both(struct cbt_map *cbt_map,
                     sector_t sector_start, sector_t sector_cnt)
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
				   (u8)cbt_map->snap_number_previous, cbt_map->read_map);
	cbt_map->state_dirty_sectors += sector_cnt;
	spin_unlock(&cbt_map->locker);

	return res;
}

size_t cbt_map_read_to_user(struct cbt_map *cbt_map, char __user *user_buff,
                            size_t offset, size_t size)
{
	size_t readed = 0;
	size_t left_size;
	size_t real_size = min((cbt_map->blk_count - offset), size);

	if (unlikely(cbt_map->is_corrupted)) {
		pr_err("CBT table was corrupted\n");
		return -EFAULT;
	}

	left_size = real_size -
		    big_buffer_copy_to_user(user_buff, offset, cbt_map->read_map, real_size);

	if (left_size == 0)
		readed = real_size;
	else {
		pr_err("Not all CBT data was read. Left [%zu] bytes\n", left_size);
		readed = real_size - left_size;
	}

	return readed;
}
