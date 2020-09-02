#pragma once
#include "range.h"

//#define RANGEVECTOR_EL_CAPACITY 250
#define RANGEVECTOR_EL_CAPACITY 1000

typedef struct rangevector_el_s
{
    struct list_head link;
    atomic_t cnt;
    range_t ranges[RANGEVECTOR_EL_CAPACITY];
}rangevector_el_t;

typedef struct rangevector_s
{
    bool use_lock;
    struct list_head ranges_head;
    atomic_t blocks_cnt;

    struct rw_semaphore lock;
}rangevector_t;

void rangevector_init( rangevector_t* rangevector, bool use_lock );

void rangevector_done( rangevector_t* rangevector );

void rangevector_cleanup( rangevector_t* rangevector );

int rangevector_add( rangevector_t* rangevector, range_t* rg );
void rangevector_sort( rangevector_t* rangevector );

int rangevector_v2p( rangevector_t* rangevector, sector_t virt_offset, sector_t virt_length, sector_t* p_phys_offset, sector_t* p_phys_length );

int rangevector_at( rangevector_t* rangevector, size_t inx, range_t* range );

sector_t rangevector_length( rangevector_t* rangevector );


#define RANGEVECTOR_READ_LOCK( rangevector )\
if ((rangevector)->use_lock){\
    down_read( &(rangevector)->lock );\
}

#define RANGEVECTOR_READ_UNLOCK( rangevector )\
if ((rangevector)->use_lock){\
    up_read( &(rangevector)->lock );\
}

#define RANGEVECTOR_WRITE_LOCK( rangevector )\
if ((rangevector)->use_lock){\
    down_write( &(rangevector)->lock );\
}

#define RANGEVECTOR_WRITE_UNLOCK( rangevector )\
if ((rangevector)->use_lock){\
    up_write( &(rangevector)->lock );\
}

#define RANGEVECTOR_FOREACH_EL_BEGIN( rangevector, el )  \
{ \
    if (!list_empty( &(rangevector)->ranges_head )){ \
        struct list_head* _list_head; \
        list_for_each( _list_head, &(rangevector)->ranges_head ){ \
        el = list_entry( _list_head, rangevector_el_t, link );

#define RANGEVECTOR_FOREACH_EL_END( ) \
        } \
    } \
}

#define RANGEVECTOR_FOREACH_BEGIN( rangevector, prange ) \
{ \
    rangevector_el_t* el; \
    size_t limit; \
    size_t inx = 0; \
    RANGEVECTOR_FOREACH_EL_BEGIN( rangevector, el ) \
        limit = (size_t)atomic_read( &el->cnt );\
        for (inx = 0; inx < limit; ++inx){\
            prange = &(el->ranges[inx]);

#define RANGEVECTOR_FOREACH_END( ) \
        }\
    RANGEVECTOR_FOREACH_EL_END( ) \
}


size_t rangevector_cnt( rangevector_t* rangevector );

range_t* rangevector_el_find_first_hit( rangevector_el_t* el, sector_t from_sect, sector_t to_sect );

