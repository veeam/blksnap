// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-blk_descr"
#include "common.h"

#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
#include "blk_descr_multidev.h"

static inline void list_assign(struct list_head *dst, struct list_head *src)
{
	dst->next = src->next;
	dst->prev = src->prev;

	src->next->prev = dst;
	src->prev->next = dst;
}

static inline void blk_descr_multidev_init(struct blk_descr_multidev *blk_descr,
					   struct list_head *rangelist)
{
	list_assign(&blk_descr->rangelist, rangelist);
}

static inline void blk_descr_multidev_done(struct blk_descr_multidev *blk_descr)
{
	while (!list_empty(&blk_descr->rangelist)) {
		struct blk_range_link_ex *rangelist =
			list_entry(blk_descr->rangelist.next, struct blk_range_link_ex, link);

		list_del(&rangelist->link);
		kfree(rangelist);
	}
}

void blk_descr_multidev_pool_init(struct blk_descr_pool *pool)
{
	blk_descr_pool_init(pool, 0);
}

static void blk_descr_multidev_cleanup(void *descr_array, size_t count)
{
	size_t inx;
	struct blk_descr_multidev *descr_multidev = descr_array;

	for (inx = 0; inx < count; ++inx)
		blk_descr_multidev_done(descr_multidev + inx);
}

void blk_descr_multidev_pool_done(struct blk_descr_pool *pool)
{
	blk_descr_pool_done(pool, blk_descr_multidev_cleanup);
}

static union blk_descr_unify blk_descr_multidev_allocate(void *descr_array, size_t index, void *arg)
{
	union blk_descr_unify blk_descr;
	struct blk_descr_multidev *multidev_blocks = descr_array;

	blk_descr.multidev = &multidev_blocks[index];

	blk_descr_multidev_init(blk_descr.multidev, (struct list_head *)arg);

	return blk_descr;
}

int blk_descr_multidev_pool_add(struct blk_descr_pool *pool, struct list_head *rangelist)
{
	union blk_descr_unify blk_descr =
		blk_descr_pool_alloc(pool, sizeof(struct blk_descr_multidev),
				     blk_descr_multidev_allocate, (void *)rangelist);

	if (NULL == blk_descr.ptr) {
		pr_err("Failed to allocate block descriptor\n");
		return -ENOMEM;
	}

	return SUCCESS;
}

union blk_descr_unify blk_descr_multidev_pool_take(struct blk_descr_pool *pool)
{
	return blk_descr_pool_take(pool, sizeof(struct blk_descr_multidev));
}

#endif //CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
