#pragma once
#include "blk_descr_unify.h"
#include "blk_descr_pool.h"

typedef struct blk_descr_mem_s
{
    blk_descr_unify_t unify;

    void* buff; //pointer to snapstore block in memory
}blk_descr_mem_t;


void blk_descr_mem_pool_init( blk_descr_pool_t* pool, size_t available_blocks );
void blk_descr_mem_pool_done( blk_descr_pool_t* pool );

int blk_descr_mem_pool_add( blk_descr_pool_t* pool, void* buffer );
blk_descr_mem_t* blk_descr_mem_pool_take( blk_descr_pool_t* pool );

