#pragma once
#include "blk_descr_pool.h"

struct blk_descr_mem
{
	void* buff; //pointer to snapstore block in memory
};


void blk_descr_mem_pool_init( blk_descr_pool_t* pool, size_t available_blocks );
void blk_descr_mem_pool_done( blk_descr_pool_t* pool );

int blk_descr_mem_pool_add( blk_descr_pool_t* pool, void* buffer );
union blk_descr_unify blk_descr_mem_pool_take( blk_descr_pool_t* pool );

