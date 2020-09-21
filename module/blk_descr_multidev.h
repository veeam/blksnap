#pragma once

#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV

#include "sector.h"
#include "blk_descr_pool.h"

struct blk_descr_multidev
{
	struct list_head rangelist;
};

typedef struct blk_range_link_ex_s
{
	struct list_head link;
	struct blk_range rg;
	struct block_device* blk_dev;

}blk_range_link_ex_t;

void blk_descr_multidev_pool_init( blk_descr_pool_t* pool );
void blk_descr_multidev_pool_done( blk_descr_pool_t* pool );


int blk_descr_multidev_pool_add( blk_descr_pool_t* pool, struct list_head* rangelist ); //allocate new empty block
union blk_descr_unify blk_descr_multidev_pool_take( blk_descr_pool_t* pool ); //take empty

#endif //CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
