#include "common.h"
#include "rangevector.h"

#define SECTION "ranges	"

void rangevector_init( rangevector_t* rangevector )
{
	init_rwsem( &rangevector->lock );

	INIT_LIST_HEAD( &rangevector->ranges_head );

	atomic_set( &rangevector->blocks_cnt, 0);
}

void rangevector_done( rangevector_t* rangevector )
{
	rangevector_cleanup( rangevector );
}

void rangevector_cleanup( rangevector_t* rangevector )
{
	while (!list_empty( &rangevector->ranges_head )){
		rangevector_el_t* pCnt = list_entry( rangevector->ranges_head.next, rangevector_el_t, link );

		list_del( &pCnt->link );
		atomic_dec( &rangevector->blocks_cnt );

		kfree( pCnt );
	}
}

int rangevector_add( rangevector_t* rangevector, struct blk_range* rg )
{
	int res = SUCCESS;

	down_write( &rangevector->lock );
	do{
		rangevector_el_t* el = NULL;

		if (!list_empty( &rangevector->ranges_head )){
			el = list_entry( rangevector->ranges_head.prev, rangevector_el_t, link );
			if ((atomic_read( &el->cnt ) == RANGEVECTOR_EL_CAPACITY))
				el = NULL;
		}

		if (el == NULL){
			el = kmalloc( sizeof( rangevector_el_t ), GFP_KERNEL );
			if (NULL == el){
				res = -ENOMEM;
				break;
			}
			atomic_set( &el->cnt, 0 );

			INIT_LIST_HEAD( &el->link );
			list_add_tail( &el->link, &rangevector->ranges_head );
			atomic_inc( &rangevector->blocks_cnt );
		}
		{
			int last_index = atomic_read( &el->cnt );

			el->ranges[last_index].ofs = rg->ofs;
			el->ranges[last_index].cnt = rg->cnt;

			atomic_inc( &el->cnt );
		}
	} while (false);
	up_write( &rangevector->lock );

	return res;
}

void rangevector_sort( rangevector_t* rangevector )
{
	//primitive pair swap sort
	bool changed;
	size_t swap_count = 0;
	size_t ranges_count = rangevector_cnt( rangevector );

	down_write( &rangevector->lock );
	do{
		struct blk_range* prange = NULL;
		struct blk_range* prange_prev = NULL;

		changed = false;
		RANGEVECTOR_FOREACH_BEGIN( rangevector, prange )
		{
			if (prange_prev != NULL){
				if (prange_prev->ofs > prange->ofs){
					struct blk_range tmp;

					tmp.ofs = prange_prev->ofs;
					tmp.cnt = prange_prev->cnt;

					prange_prev->ofs = prange->ofs;
					prange_prev->cnt = prange->cnt;

					prange->ofs = tmp.ofs;
					prange->cnt = tmp.cnt;
					changed = true;

					++swap_count;
				}
			}
			prange_prev = prange;
		}
		RANGEVECTOR_FOREACH_END( );
	} while (changed);
	up_write( &rangevector->lock );

	log_tr_sz( "Sort zero ranges count=", ranges_count );
	log_tr_sz( "Swap count=", swap_count );
}
/*
sector_t rangevector_length( rangevector_t* rangevector )
{
	struct blk_range* prange = NULL;
	sector_t length_sect = 0;
	down_read( &rangevector->lock );
	RANGEVECTOR_FOREACH_BEGIN( rangevector, prange )
	{
		length_sect += prange->cnt;
	}
	RANGEVECTOR_FOREACH_END( );
	up_read( &rangevector->lock );
	return length_sect;
}
*/
size_t rangevector_cnt( rangevector_t* rangevector )
{
	size_t cnt = 0;
	rangevector_el_t* el;

	down_read( &rangevector->lock );
	RANGEVECTOR_FOREACH_EL_BEGIN( rangevector, el )
	{
		cnt += (size_t)atomic_read( &el->cnt );
	}
	RANGEVECTOR_FOREACH_EL_END( );
	up_read( &rangevector->lock );
	return cnt;
}

struct blk_range* rangevector_el_find_first_hit( rangevector_el_t* el, sector_t from_sect, sector_t to_sect )
{
	struct blk_range* found = NULL;
	size_t index = 0;
	size_t limit_min = 0;
	size_t limit_max = atomic_read( &el->cnt );

	index = (limit_max - limit_min) / 2;

	do{
		struct blk_range* rg = &el->ranges[index];

		if ((rg->ofs + rg->cnt) <= from_sect){
			size_t increment = (limit_max - index) / 2;
			if (increment == 0)
				break;

			limit_min = index;
			index += increment;
			continue;
		}

		if (rg->ofs >= to_sect){
			size_t decrement = (index - limit_min) / 2;
			if (decrement == 0)
				break;

			limit_max = index;
			index -= decrement;
			continue;
		}

		found = rg; //some one found
		break;
	} while (true);

	//check previous ranges
	if (found != NULL){
		while (index > 0){
			struct blk_range* rg;
			--index;
			rg = &el->ranges[index];

			if ((rg->ofs + rg->cnt) <= from_sect)
				break;

			if (rg->ofs >= to_sect)
				break;

			found = rg; //better found
		}
	}

	return found;
}

