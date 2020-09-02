#include "stdafx.h"
#include "mem_alloc.h"

#define SECTION "mem_alloc "

#ifdef VEEAMSNAP_MEMORY_LEAK_CONTROL

atomic_t g_mem_cnt;

void dbg_mem_init(void )
{
    atomic_set( &g_mem_cnt, 0 );
}

void dbg_mem_print_state( void )
{
    log_tr( "" );
    log_tr( "memory allocation state:" );

    log_tr_d( "mem_cnt=", atomic_read( &g_mem_cnt ) );
}

volatile long dbg_mem_track = false;
void dbg_mem_track_on( void )
{
    dbg_mem_track = true;
    log_tr( "memory allocation tracing turned on" );
}
void dbg_mem_track_off( void )
{
    dbg_mem_track = false;
    log_tr( "memory allocation tracing turned off" );
}


void dbg_kfree( const void *ptr )
{
    if (dbg_mem_track){
        pr_warn( "kfree %p", ptr );
    }
    if (ptr){
        atomic_dec( &g_mem_cnt );
        kfree( ptr );
    }
}


void * dbg_kzalloc( size_t size, gfp_t flags )
{
    void* ptr = kzalloc( size, flags );
    if (ptr)
        atomic_inc( &g_mem_cnt );

    if (dbg_mem_track){
        pr_warn( "kzalloc %p", ptr );
    }
    return ptr;
}

void * dbg_kmalloc( size_t size, gfp_t flags )
{
    void* ptr = kmalloc( size, flags );
    if (ptr)
        atomic_inc( &g_mem_cnt );

    if (dbg_mem_track){
        pr_warn( "kmalloc %p", ptr );
    }
    return ptr;
}

#endif

void * dbg_kmalloc_huge( size_t max_size, size_t min_size, gfp_t flags, size_t* p_allocated_size )
{
    void * ptr = NULL;

    do{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,13,0)
        ptr = dbg_kmalloc( max_size, flags | __GFP_NOWARN | __GFP_REPEAT );
#else
        ptr = dbg_kmalloc( max_size, flags | __GFP_NOWARN | __GFP_RETRY_MAYFAIL );
#endif
        if (ptr != NULL){
            *p_allocated_size = max_size;
            return ptr;
        }
        log_err_sz( "Failed to allocate buffer size=", max_size );
        max_size = max_size >> 1;
    } while (max_size >= min_size);
    log_err( "Failed to allocate buffer." );
    return NULL;
}

