#pragma once
#include "sector.h"
#include "blk_descr_pool.h"

struct blk_descr_file {
	struct list_head rangelist;
};

typedef struct blk_range_link_s {
	struct list_head link;
	struct blk_range rg;
} blk_range_link_t;

void blk_descr_file_pool_init(blk_descr_pool_t *pool);
void blk_descr_file_pool_done(blk_descr_pool_t *pool);

/* 
 * allocate new empty block in pool
 */
int blk_descr_file_pool_add(blk_descr_pool_t *pool, struct list_head *rangelist);

/* 
 * take empty block from pool
 */
union blk_descr_unify blk_descr_file_pool_take(blk_descr_pool_t *pool); 
