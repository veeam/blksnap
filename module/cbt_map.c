// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-cbt_map"
#include "common.h"
#include "cbt_map.h"

static int _cbt_map_allocate(struct cbt_map *cbt_map, unsigned int cbt_sect_in_block_degree,
		     sector_t device_capacity)
{
	sector_t size_mod;

	cbt_map->sect_in_block_degree = cbt_sect_in_block_degree;
	cbt_map->device_capacity = device_capacity;
	cbt_map->map_size = (device_capacity >> (sector_t)cbt_sect_in_block_degree);

	pr_info("Allocate CBT map of %zu\n", cbt_map->map_size);

	size_mod = (device_capacity & ((sector_t)(1 << cbt_sect_in_block_degree) - 1));
	if (size_mod)
		cbt_map->map_size++;

	cbt_map->read_map = big_buffer_alloc(cbt_map->map_size, GFP_KERNEL);
	if (cbt_map->read_map != NULL)
		big_buffer_memset(cbt_map->read_map, 0);

	cbt_map->write_map = big_buffer_alloc(cbt_map->map_size, GFP_KERNEL);
	if (cbt_map->write_map != NULL)
		big_buffer_memset(cbt_map->write_map, 0);

	if ((cbt_map->read_map == NULL) || (cbt_map->write_map == NULL)) {
		pr_err("Cannot allocate CBT map. map_size=%zu\n", cbt_map->map_size);
		return -ENOMEM;
	}

	cbt_map->snap_number_previous = 0;
	cbt_map->snap_number_active = 1;
	generate_random_uuid(cbt_map->generationId.b);
	cbt_map->active = true;

	cbt_map->state_changed_sectors = 0;
	cbt_map->state_dirty_sectors = 0;

	return SUCCESS;
}

static void _cbt_map_deallocate(struct cbt_map *cbt_map)
{
	if (cbt_map->read_map != NULL) {
		big_buffer_free(cbt_map->read_map);
		cbt_map->read_map = NULL;
	}

	if (cbt_map->write_map != NULL) {
		big_buffer_free(cbt_map->write_map);
		cbt_map->write_map = NULL;
	}

	cbt_map->active = false;
}

static void cbt_map_destroy(struct cbt_map *cbt_map)
{
	pr_info("CBT map destroy\n");
	if (cbt_map != NULL) {
		_cbt_map_deallocate(cbt_map);

		kfree(cbt_map);
	}
}

struct cbt_map *cbt_map_create(unsigned int cbt_sect_in_block_degree, sector_t device_capacity)
{
	struct cbt_map *cbt_map = NULL;

	pr_info("CBT map create\n");

	cbt_map = kzalloc(sizeof(struct cbt_map), GFP_KERNEL);
	if (cbt_map == NULL)
		return NULL;

	if (_cbt_map_allocate(cbt_map, cbt_sect_in_block_degree, device_capacity) != SUCCESS) {
		cbt_map_destroy(cbt_map);
		return NULL;
	}

	spin_lock_init(&cbt_map->locker);
	init_rwsem(&cbt_map->rw_lock);
	kref_init(&cbt_map->refcount);

	return cbt_map;
}

static void _cbt_map_destroy_cb(struct kref *kref)
{
	cbt_map_destroy(container_of(kref, struct cbt_map, refcount));
}

struct cbt_map *cbt_map_get_resource(struct cbt_map *cbt_map)
{
	if (cbt_map)
		kref_get(&cbt_map->refcount);

	return cbt_map;
}

void cbt_map_put_resource(struct cbt_map *cbt_map)
{
	if (cbt_map)
		kref_put(&cbt_map->refcount, _cbt_map_destroy_cb);
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

static int _cbt_map_set(struct cbt_map *cbt_map, sector_t sector_start, sector_t sector_cnt,
		 u8 snap_number, struct big_buffer *map)
{
	int res = SUCCESS;
	size_t cbt_block;
	size_t cbt_block_first = (size_t)(sector_start >> cbt_map->sect_in_block_degree);
	size_t cbt_block_last = (size_t)((sector_start + sector_cnt - 1) >>
					 cbt_map->sect_in_block_degree); //inclusive

	for (cbt_block = cbt_block_first; cbt_block <= cbt_block_last; ++cbt_block) {
		if (cbt_block < cbt_map->map_size) {
			u8 num;

			res = big_buffer_byte_get(map, cbt_block, &num);
			if (res == SUCCESS)
				if (num < snap_number)
					res = big_buffer_byte_set(map, cbt_block, snap_number);
		} else
			res = -EINVAL;

		if (res != SUCCESS) {
			pr_err("Block index is too large. #%zu was demanded, map size %zu\n",
			       cbt_block, cbt_map->map_size);
			break;
		}
	}
	return res;
}

int cbt_map_set(struct cbt_map *cbt_map, sector_t sector_start, sector_t sector_cnt)
{
	int res = SUCCESS;

	spin_lock(&cbt_map->locker);

	res = _cbt_map_set(cbt_map, sector_start, sector_cnt, (u8)cbt_map->snap_number_active,
			   cbt_map->write_map);
	cbt_map->state_changed_sectors += sector_cnt;

	spin_unlock(&cbt_map->locker);
	return res;
}

int cbt_map_set_both(struct cbt_map *cbt_map, sector_t sector_start, sector_t sector_cnt)
{
	int res = SUCCESS;

	spin_lock(&cbt_map->locker);

	res = _cbt_map_set(cbt_map, sector_start, sector_cnt,
			   (u8)cbt_map->snap_number_active, cbt_map->write_map);
	if (res == SUCCESS)
		res = _cbt_map_set(cbt_map, sector_start, sector_cnt,
				   (u8)cbt_map->snap_number_previous, cbt_map->read_map);
	cbt_map->state_dirty_sectors += sector_cnt;

	spin_unlock(&cbt_map->locker);
	return res;
}

size_t cbt_map_read_to_user(struct cbt_map *cbt_map, void __user *user_buff, size_t offset,
			    size_t size)
{
	size_t readed = 0;
	size_t left_size;
	size_t real_size = min((cbt_map->map_size - offset), size);

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
