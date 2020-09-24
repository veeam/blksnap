/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV

#include "blk_descr_pool.h"

struct blk_descr_multidev {
	struct list_head rangelist;
};

struct blk_range_link_ex {
	struct list_head link;
	struct blk_range rg;
	struct block_device *blk_dev;
};

void blk_descr_multidev_pool_init(struct blk_descr_pool *pool);
void blk_descr_multidev_pool_done(struct blk_descr_pool *pool);

int blk_descr_multidev_pool_add(struct blk_descr_pool *pool,
				struct list_head *rangelist); //allocate new empty block
union blk_descr_unify blk_descr_multidev_pool_take(struct blk_descr_pool *pool); //take empty

#endif //CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
