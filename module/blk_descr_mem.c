#include "stdafx.h"
#include "blk_descr_mem.h"

#define SECTION "blk_descr "

void blk_descr_mem_init( blk_descr_mem_t* blk_descr, void* ptr )
{
    blk_descr_unify_init( &blk_descr->unify );

    blk_descr->buff = ptr;
}

void blk_descr_mem_done( blk_descr_mem_t* blk_descr )
{
    blk_descr->buff = NULL;
}

void blk_descr_mem_pool_init( blk_descr_pool_t* pool, size_t available_blocks )
{
    blk_descr_pool_init( pool, available_blocks );
}

void blk_descr_mem_cleanup( blk_descr_unify_t* blocks, size_t count )
{
    size_t inx;
    blk_descr_mem_t* mem_blocks = (blk_descr_mem_t*)blocks;

    for (inx = 0; inx < count; ++inx)
        blk_descr_mem_done( mem_blocks + inx );
}

void blk_descr_mem_pool_done( blk_descr_pool_t* pool )
{
    blk_descr_pool_done( pool, blk_descr_mem_cleanup );
}

blk_descr_unify_t* blk_descr_mem_alloc( blk_descr_unify_t* blocks, size_t index, void* arg )
{
    blk_descr_mem_t* mem_blocks = (blk_descr_mem_t*)blocks;
    blk_descr_mem_t* block_mem = &mem_blocks[index];

    blk_descr_mem_init( block_mem, (void*)arg );

    return (blk_descr_unify_t*)block_mem;
}

int blk_descr_mem_pool_add( blk_descr_pool_t* pool, void* buffer )
{
    if (NULL == blk_descr_pool_alloc( pool, sizeof( blk_descr_mem_t ), blk_descr_mem_alloc, buffer ))
        return -ENOMEM;
    return SUCCESS;
}

blk_descr_mem_t* blk_descr_mem_pool_take( blk_descr_pool_t* pool )
{
    return (blk_descr_mem_t*)blk_descr_pool_take( pool, sizeof( blk_descr_mem_t ) );
}
