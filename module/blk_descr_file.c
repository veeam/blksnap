// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-blk_descr"
#include "common.h"

#include "blk_descr_file.h"

static inline void list_assign(struct list_head *dst, struct list_head *src)
{
	dst->next = src->next;
	dst->prev = src->prev;

	src->next->prev = dst;
	src->prev->next = dst;
}

static inline void blk_descr_file_init(struct blk_descr_file *blk_descr,
				       struct list_head *rangelist)
{
	list_assign(&blk_descr->rangelist, rangelist);
}

static inline void blk_descr_file_done(struct blk_descr_file *blk_descr)
{
	struct blk_range_link *range_link;

	while (!list_empty(&blk_descr->rangelist)) {
		range_link = list_entry(blk_descr->rangelist.next, struct blk_range_link, link);

		list_del(&range_link->link);
		kfree(range_link);
	}
}

void blk_descr_file_pool_init(struct blk_descr_pool *pool)
{
	blk_descr_pool_init(pool, 0);
}

void _blk_descr_file_cleanup(void *descr_array, size_t count)
{
	size_t inx;
	struct blk_descr_file *file_blocks = descr_array;

	for (inx = 0; inx < count; ++inx)
		blk_descr_file_done(file_blocks + inx);
}

void blk_descr_file_pool_done(struct blk_descr_pool *pool)
{
	blk_descr_pool_done(pool, _blk_descr_file_cleanup);
}

static union blk_descr_unify _blk_descr_file_allocate(void *descr_array, size_t index, void *arg)
{
	union blk_descr_unify blk_descr;
	struct blk_descr_file *file_blocks = descr_array;

	blk_descr.file = &file_blocks[index];

	blk_descr_file_init(blk_descr.file, (struct list_head *)arg);

	return blk_descr;
}

int blk_descr_file_pool_add(struct blk_descr_pool *pool, struct list_head *rangelist)
{
	union blk_descr_unify blk_descr;

	blk_descr = blk_descr_pool_alloc(pool, sizeof(struct blk_descr_file),
					 _blk_descr_file_allocate, (void *)rangelist);
	if (blk_descr.ptr == NULL) {
		pr_err("Failed to allocate block descriptor\n");
		return -ENOMEM;
	}

	return SUCCESS;
}

union blk_descr_unify blk_descr_file_pool_take(struct blk_descr_pool *pool)
{
	return blk_descr_pool_take(pool, sizeof(struct blk_descr_file));
}
