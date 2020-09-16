#pragma once
#include "sector.h"

//#define RANGEVECTOR_EL_CAPACITY 250
#define RANGEVECTOR_EL_CAPACITY 1000

typedef struct rangevector_el_s
{
	struct list_head link;
	atomic_t cnt;
	struct blk_range ranges[RANGEVECTOR_EL_CAPACITY];
}rangevector_el_t;

typedef struct rangevector_s
{
	struct list_head ranges_head;
	atomic_t blocks_cnt;

	struct rw_semaphore lock;
}rangevector_t;

void rangevector_init( rangevector_t* rangevector);

void rangevector_done( rangevector_t* rangevector );

void rangevector_cleanup( rangevector_t* rangevector );

int rangevector_add( rangevector_t* rangevector, struct blk_range* rg );

void rangevector_sort( rangevector_t* rangevector );

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

struct blk_range* rangevector_el_find_first_hit( rangevector_el_t* el, sector_t from_sect, sector_t to_sect );

