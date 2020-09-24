// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-blk_descr"
#include "common.h"
#include "blk_descr_mem.h"

#define SECTION "blk_descr "

static inline void blk_descr_mem_init(struct blk_descr_mem *blk_descr, void *ptr)
{
	blk_descr->buff = ptr;
}

static inline void blk_descr_mem_done(struct blk_descr_mem *blk_descr)
{
	blk_descr->buff = NULL;
}

void blk_descr_mem_pool_init(struct blk_descr_pool *pool, size_t available_blocks)
{
	blk_descr_pool_init(pool, available_blocks);
}

void blk_descr_mem_cleanup(void *descr_array, size_t count)
{
	size_t inx;
	struct blk_descr_mem *mem_blocks = descr_array;

	for (inx = 0; inx < count; ++inx)
		blk_descr_mem_done(mem_blocks + inx);
}

void blk_descr_mem_pool_done(struct blk_descr_pool *pool)
{
	blk_descr_pool_done(pool, blk_descr_mem_cleanup);
}

static union blk_descr_unify blk_descr_mem_alloc(void *descr_array, size_t index, void *arg)
{
	union blk_descr_unify blk_descr;
	struct blk_descr_mem *mem_blocks = descr_array;

	blk_descr.mem = &mem_blocks[index];

	blk_descr_mem_init(blk_descr.mem, (void *)arg);

	return blk_descr;
}

int blk_descr_mem_pool_add(struct blk_descr_pool *pool, void *buffer)
{
	union blk_descr_unify blk_descr = blk_descr_pool_alloc(pool, sizeof(struct blk_descr_mem),
							       blk_descr_mem_alloc, buffer);

	if (NULL == blk_descr.ptr) {
		pr_err("Failed to allocate block descriptor\n");
		return -ENOMEM;
	}

	return SUCCESS;
}

union blk_descr_unify blk_descr_mem_pool_take(struct blk_descr_pool *pool)
{
	return blk_descr_pool_take(pool, sizeof(struct blk_descr_mem));
}
