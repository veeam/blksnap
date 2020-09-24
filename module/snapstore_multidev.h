/* SPDX-License-Identifier: GPL-2.0 */
#pragma once

#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV

#include "blk_deferred.h"
#include "blk_descr_multidev.h"

struct snapstore_multidev {
	struct list_head devicelist; //for mapping device id to opened device struct pointer
	spinlock_t devicelist_lock;

	struct blk_descr_pool pool;
};

int snapstore_multidev_create(struct snapstore_multidev **p_file);

void snapstore_multidev_destroy(struct snapstore_multidev *file);

struct block_device *snapstore_multidev_get_device(struct snapstore_multidev *multidev,
						   dev_t dev_id);
#endif
