#include "stdafx.h"

#ifdef SNAPSTORE_MULTIDEV

#include "blk_descr_multidev.h"

#define SECTION "blk_descr "
#include "log_format.h"

void blk_descr_multidev_init( blk_descr_multidev_t* blk_descr, rangelist_ex_t* rangelist )
{
    blk_descr_unify_init( &blk_descr->unify );

    rangelist_ex_copy( &blk_descr->rangelist, rangelist );
}

void blk_descr_multidev_done( blk_descr_multidev_t* blk_descr )
{
    rangelist_ex_done( &blk_descr->rangelist );
}

void blk_descr_multidev_pool_init( blk_descr_pool_t* pool )
{
    blk_descr_pool_init( pool, 0 );
}

void _blk_descr_multidev_cleanup( blk_descr_unify_t* blocks, size_t count )
{
    size_t inx;
    blk_descr_multidev_t* file_blocks = (blk_descr_multidev_t*)blocks;

    for (inx = 0; inx < count; ++inx)
        blk_descr_multidev_done( file_blocks + inx );
}

void blk_descr_multidev_pool_done( blk_descr_pool_t* pool )
{
    blk_descr_pool_done( pool, _blk_descr_multidev_cleanup );
}

blk_descr_unify_t* _blk_descr_multidev_alloc( blk_descr_unify_t* blocks, size_t index, void* arg )
{
    blk_descr_multidev_t* multidev_blocks = (blk_descr_multidev_t*)blocks;
    blk_descr_multidev_t* block_file = &multidev_blocks[index];

    blk_descr_multidev_init( block_file, (rangelist_ex_t*)arg );

    return (blk_descr_unify_t*)block_file;
}

int blk_descr_multidev_pool_add( blk_descr_pool_t* pool, rangelist_ex_t* rangelist )
{
    blk_descr_multidev_t* blk_descr;

    blk_descr = (blk_descr_multidev_t*)blk_descr_pool_alloc( pool, sizeof( blk_descr_multidev_t ), _blk_descr_multidev_alloc, (void*)rangelist );
    if (blk_descr == NULL){
        log_err( "Failed to allocate block descriptor" );
        return -ENOMEM;
    }

    return SUCCESS;
}

blk_descr_multidev_t* blk_descr_multidev_pool_take( blk_descr_pool_t* pool )
{
    return (blk_descr_multidev_t*)blk_descr_pool_take( pool, sizeof( blk_descr_multidev_t ) );
}

#endif //SNAPSTORE_MULTIDEV
