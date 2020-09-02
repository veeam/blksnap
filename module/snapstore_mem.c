#include "stdafx.h"
#include "snapstore_mem.h"

#define SECTION "snapstore "
#include "log_format.h"

typedef struct buffer_el_s{
    content_t content;
    void* buff;
}buffer_el_t;


snapstore_mem_t* snapstore_mem_create( size_t available_blocks )
{
    int res;
    snapstore_mem_t* mem = NULL;

    mem = dbg_kzalloc( sizeof( snapstore_mem_t ), GFP_KERNEL );
    if (mem == NULL)
        return NULL;
    
    blk_descr_mem_pool_init( &mem->pool, available_blocks );

    mem->blocks_limit = available_blocks;
    do{
        res = container_init( &mem->blocks_list, sizeof( buffer_el_t ) );
        if (res != SUCCESS)
            break;


    } while (false);
    if (res != SUCCESS){
        dbg_kfree( mem );
        mem = NULL;
    }

    return mem;
}

void snapstore_mem_destroy( snapstore_mem_t* mem )
{
    if (mem != NULL){
        buffer_el_t* buffer_el = NULL;

        while ( NULL != (buffer_el = (buffer_el_t*)container_get_first( &mem->blocks_list )) )
        {
            //dbg_vfree( buffer_el->buff, SNAPSTORE_BLK_SIZE * SECTOR512 );
            vfree( buffer_el->buff );
            content_free( &buffer_el->content );
        }

        if (SUCCESS != container_done( &mem->blocks_list ))
            log_err( "Unable to perform snapstore cleanup in memory: memory blocks container is not empty" );

        blk_descr_mem_pool_done( &mem->pool );

        dbg_kfree( mem );
    }
}

void* snapstore_mem_get_block( snapstore_mem_t* mem )
{
    buffer_el_t* buffer_el;

    if (mem->blocks_allocated >= mem->blocks_limit){
        log_err_format( "Unable to get block from snapstore in memory: block limit is reached, allocated %ld, limit %ld", mem->blocks_allocated, mem->blocks_limit );
        return NULL;
    }

    buffer_el = (buffer_el_t*)content_new( &mem->blocks_list );
    if (buffer_el == NULL)
        return NULL;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,8,0)
    buffer_el->buff = __vmalloc(SNAPSTORE_BLK_SIZE * SECTOR_SIZE, GFP_NOIO);
#else
    buffer_el->buff = __vmalloc( SNAPSTORE_BLK_SIZE * SECTOR_SIZE, GFP_NOIO, PAGE_KERNEL );
#endif
    if (buffer_el->buff == NULL){
        content_free( &buffer_el->content );
        return NULL;
    }

    ++mem->blocks_allocated;
    if (0 == (mem->blocks_allocated & 0x7F)){
        log_tr_format( "%d MiB was allocated", mem->blocks_allocated );
    }

    container_push_back( &mem->blocks_list, &buffer_el->content );
    return buffer_el->buff;
}
