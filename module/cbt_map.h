/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "big_buffer.h"
#include <linux/kref.h>
#include <linux/uuid.h>

struct cbt_map {
	struct kref refcount;

	spinlock_t locker;

	size_t sect_in_block_degree;
	sector_t device_capacity;
	size_t map_size;

	struct big_buffer *read_map;
	struct big_buffer *write_map;

	unsigned long snap_number_active;
	unsigned long snap_number_previous;
	uuid_t generationId;

	bool active;

	struct rw_semaphore rw_lock;

	sector_t state_changed_sectors;
	sector_t state_dirty_sectors;
};

struct cbt_map *cbt_map_create(unsigned int cbt_sect_in_block_degree, sector_t device_capacity);
int cbt_map_reset(struct cbt_map *cbt_map, unsigned int cbt_sect_in_block_degree,
		  sector_t device_capacity);

struct cbt_map *cbt_map_get_resource(struct cbt_map *cbt_map);
void cbt_map_put_resource(struct cbt_map *cbt_map);

void cbt_map_switch(struct cbt_map *cbt_map);
int cbt_map_set(struct cbt_map *cbt_map, sector_t sector_start, sector_t sector_cnt);
int cbt_map_set_both(struct cbt_map *cbt_map, sector_t sector_start, sector_t sector_cnt);

size_t cbt_map_read_to_user(struct cbt_map *cbt_map, void __user *user_buffer, size_t offset,
			    size_t size);

static inline void cbt_map_read_lock(struct cbt_map *cbt_map)
{
	down_read(&cbt_map->rw_lock);
};

static inline void cbt_map_read_unlock(struct cbt_map *cbt_map)
{
	up_read(&cbt_map->rw_lock);
};

static inline void cbt_map_write_lock(struct cbt_map *cbt_map)
{
	down_write(&cbt_map->rw_lock);
};

static inline void cbt_map_write_unlock(struct cbt_map *cbt_map)
{
	up_write(&cbt_map->rw_lock);
};
