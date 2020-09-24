#pragma once
#include "sector.h"
#include "blk_descr_pool.h"

struct blk_descr_file {
	struct list_head rangelist;
};

struct blk_range_link {
	struct list_head link;
	struct blk_range rg;
};

void blk_descr_file_pool_init(struct blk_descr_pool *pool);
void blk_descr_file_pool_done(struct blk_descr_pool *pool);

/* 
 * allocate new empty block in pool
 */
int blk_descr_file_pool_add(struct blk_descr_pool *pool, struct list_head *rangelist);

/* 
 * take empty block from pool
 */
union blk_descr_unify blk_descr_file_pool_take(struct blk_descr_pool *pool);
