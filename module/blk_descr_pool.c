#include "common.h"
#include "blk_descr_pool.h"
#include "snapstore_blk.h"

static void * kmalloc_huge( size_t max_size, size_t min_size, gfp_t flags, size_t* p_allocated_size )
{
	void * ptr = NULL;

	do{
		ptr = kmalloc( max_size, flags | __GFP_NOWARN | __GFP_RETRY_MAYFAIL );

		if (ptr != NULL){
			*p_allocated_size = max_size;
			return ptr;
		}
		pr_err( "Failed to allocate buffer size=%lu\n", max_size );
		max_size = max_size >> 1;
	} while (max_size >= min_size);
	pr_err( "Failed to allocate buffer." );
	return NULL;
}

static pool_el_t* pool_el_alloc( size_t blk_descr_size )
{
	size_t el_size;
	pool_el_t* el = (pool_el_t*)kmalloc_huge( 8*PAGE_SIZE, PAGE_SIZE, GFP_NOIO, &el_size );
	if (NULL == el)
		return NULL;

	el->capacity = (el_size - sizeof( pool_el_t )) / blk_descr_size;
	el->used_cnt = 0;

	INIT_LIST_HEAD( &el->link );

	return el;
}

static void _pool_el_free( pool_el_t* el )
{
	if (el != NULL)
		kfree( el );
}

void blk_descr_pool_init( blk_descr_pool_t* pool, size_t available_blocks)
{
	mutex_init(&pool->lock);

	INIT_LIST_HEAD( &pool->head );

	pool->blocks_cnt = 0;

	pool->total_cnt = available_blocks;
	pool->take_cnt = 0;
}

void blk_descr_pool_done( blk_descr_pool_t* pool, blk_descr_cleanup_t blocks_cleanup )
{
	mutex_lock( &pool->lock );
	while (!list_empty( &pool->head ))
	{
		pool_el_t* el = list_entry( pool->head.next, pool_el_t, link );
		if (el == NULL)
			break;

		list_del( &el->link );
		--pool->blocks_cnt;

		pool->total_cnt -= el->used_cnt;

		blocks_cleanup( el->descr_array, el->used_cnt );

		_pool_el_free( el );

	}
	mutex_unlock(&pool->lock);
}

union blk_descr_unify blk_descr_pool_alloc( blk_descr_pool_t* pool, size_t blk_descr_size, blk_descr_allocate_cb block_alloc, void* arg )
{
	union blk_descr_unify blk_descr = {NULL};

	mutex_lock(&pool->lock);
	do{
		pool_el_t* el = NULL;

		if (!list_empty( &pool->head )){
			el = list_entry( pool->head.prev, pool_el_t, link );
			if (el->used_cnt == el->capacity)
				el = NULL;
		}

		if (el == NULL){
			el = pool_el_alloc( blk_descr_size );
			if (NULL == el)
				break;

			list_add_tail( &el->link, &pool->head );

			++pool->blocks_cnt;
		}

		blk_descr = block_alloc( el->descr_array, el->used_cnt, arg );

		++el->used_cnt;
		++pool->total_cnt;

	} while (false);
	mutex_unlock(&pool->lock);

	return blk_descr;
}


#define _FOREACH_EL_BEGIN( pool, el )  \
if (!list_empty( &(pool)->head )){ \
	struct list_head* _list_head; \
	list_for_each( _list_head, &(pool)->head ){ \
		el = list_entry( _list_head, pool_el_t, link );

#define _FOREACH_EL_END( ) \
	} \
}

static union blk_descr_unify __blk_descr_pool_at( blk_descr_pool_t* pool, size_t blk_descr_size, size_t index )
{
	union blk_descr_unify bkl_descr = {NULL};
	size_t curr_inx = 0;
	pool_el_t* el;

	_FOREACH_EL_BEGIN( pool, el )
	{
		if ((index >= curr_inx) && (index < (curr_inx + el->used_cnt))){
			bkl_descr.ptr = el->descr_array + (index - curr_inx) * blk_descr_size;
			break;
		}
		curr_inx += el->used_cnt;
	}
	_FOREACH_EL_END( );

	return bkl_descr;
}

union blk_descr_unify blk_descr_pool_take( blk_descr_pool_t* pool, size_t blk_descr_size )
{
	union blk_descr_unify result = {NULL};
	mutex_lock(&pool->lock);
	do{
		if (pool->take_cnt >= pool->total_cnt){
			pr_err("Unable to get block descriptor: not enough descriptors. Already took %ld, total %ld\n", pool->take_cnt, pool->total_cnt);
			break;
		}

		result = __blk_descr_pool_at(pool, blk_descr_size, pool->take_cnt);
		if (result.ptr == NULL){
			pr_err("Unable to get block descriptor: not enough descriptors. Already took %ld, total %ld\n", pool->take_cnt, pool->total_cnt);
			break;
		}

		++pool->take_cnt;
	} while (false);
	mutex_unlock(&pool->lock);
	return result;
}


bool blk_descr_pool_check_halffill( blk_descr_pool_t* pool, sector_t empty_limit, sector_t* fill_status )
{
	size_t empty_blocks = (pool->total_cnt - pool->take_cnt);

	*fill_status = (sector_t)(pool->take_cnt) << snapstore_block_shift();

	return (empty_blocks < (size_t)(empty_limit >> snapstore_block_shift()));
}
