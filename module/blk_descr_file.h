#pragma once
#include "sector.h"
#include "blk_descr_unify.h"
#include "blk_descr_pool.h"

typedef struct blk_descr_file_s
{
	blk_descr_unify_t unify;

	struct list_head rangelist;
}blk_descr_file_t;

typedef struct blk_range_link_s
{
	struct list_head link;
	struct blk_range rg;
}blk_range_link_t;

void blk_descr_file_pool_init( blk_descr_pool_t* pool );
void blk_descr_file_pool_done( blk_descr_pool_t* pool );


int blk_descr_file_pool_add( blk_descr_pool_t* pool, struct list_head* rangelist ); //allocate new empty block
blk_descr_file_t* blk_descr_file_pool_take( blk_descr_pool_t* pool ); //take empty
