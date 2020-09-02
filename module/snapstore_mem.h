#pragma once

#include "container.h"
#include "blk_descr_mem.h"


typedef struct snapstore_mem_s{

    container_t blocks_list;
    size_t blocks_limit;
    size_t blocks_allocated;

    blk_descr_pool_t pool;
}snapstore_mem_t;

snapstore_mem_t* snapstore_mem_create( size_t available_blocks );

void snapstore_mem_destroy( snapstore_mem_t* mem );

void* snapstore_mem_get_block( snapstore_mem_t* mem );

