#pragma once

#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV

#include "range.h"
#include "rangelist_ex.h"
#include "blk_descr_unify.h"
#include "blk_descr_pool.h"

typedef struct blk_descr_multidev_s
{
	blk_descr_unify_t unify;

	rangelist_ex_t rangelist;
}blk_descr_multidev_t;


void blk_descr_multidev_pool_init( blk_descr_pool_t* pool );
void blk_descr_multidev_pool_done( blk_descr_pool_t* pool );


int blk_descr_multidev_pool_add( blk_descr_pool_t* pool, rangelist_ex_t* rangelist ); //allocate new empty block
blk_descr_multidev_t* blk_descr_multidev_pool_take( blk_descr_pool_t* pool ); //take empty

#endif //CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
