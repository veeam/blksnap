#pragma once

#ifdef VEEAMSNAP_MEMORY_LEAK_CONTROL

void dbg_mem_track_on( void );
void dbg_mem_track_off( void );

extern atomic_t g_mem_cnt;

void dbg_mem_init( void );
void dbg_mem_print_state( void );

void dbg_kfree( const void *ptr );
void *dbg_kzalloc( size_t size, gfp_t flags );
void *dbg_kmalloc( size_t size, gfp_t flags );

#else

static inline void dbg_kfree( const void *ptr )
{
    if (NULL != ptr)
        kfree( ptr );
};
static inline void *dbg_kzalloc( size_t size, gfp_t flags )
{
    void* ptr = kzalloc( size, flags );
    return ptr;
};
static inline void *dbg_kmalloc( size_t size, gfp_t flags )
{
    void* ptr = kmalloc( size, flags );
    return ptr;
};

#endif



void * dbg_kmalloc_huge( size_t max_size, size_t min_size, gfp_t flags, size_t* p_allocated_size );

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)
#define mem_kmap_atomic(__page) kmap_atomic(__page,KM_BOUNCE_READ)
#define mem_kunmap_atomic(__mem) kunmap_atomic(__mem,KM_BOUNCE_READ)
#else
#define mem_kmap_atomic(__page) kmap_atomic(__page)
#define mem_kunmap_atomic(__mem) kunmap_atomic(__mem)
#endif
