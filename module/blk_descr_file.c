#include "common.h"
#include "blk_descr_file.h"

#define SECTION "blk_descr "
#include "log_format.h"


void blk_descr_file_init( blk_descr_file_t* blk_descr, struct list_head *rangelist )
{
	blk_descr_unify_init( &blk_descr->unify );

	memcpy( &blk_descr->rangelist, rangelist, sizeof(struct list_head));
}

void blk_descr_file_done( blk_descr_file_t* blk_descr )
{
	while (!list_empty( &blk_descr->rangelist )) {
		blk_range_link_t *range_link = list_entry( blk_descr->rangelist.next, blk_range_link_t, link );

		list_del( &range_link->link );
		kfree( range_link );
	}
}

void blk_descr_file_pool_init( blk_descr_pool_t* pool )
{
	blk_descr_pool_init( pool, 0 );
}

void _blk_descr_file_cleanup(blk_descr_unify_t* blocks, size_t count)
{
	size_t inx;
	blk_descr_file_t* file_blocks = (blk_descr_file_t*)blocks;

	for (inx = 0; inx < count; ++inx)
		blk_descr_file_done( file_blocks + inx );
}

void blk_descr_file_pool_done( blk_descr_pool_t* pool )
{
	blk_descr_pool_done( pool, _blk_descr_file_cleanup );
}

static
blk_descr_unify_t* _blk_descr_file_allocate( blk_descr_unify_t* blocks, size_t index, void* arg )
{
	blk_descr_file_t* file_blocks = (blk_descr_file_t*)blocks;
	blk_descr_file_t* block_file = &file_blocks[index];

	blk_descr_file_init( block_file, (struct list_head*)arg );

	return (blk_descr_unify_t*)block_file;
}

int blk_descr_file_pool_add( blk_descr_pool_t* pool, struct list_head* rangelist )
{
	blk_descr_file_t* blk_descr;

	blk_descr = (blk_descr_file_t*)blk_descr_pool_alloc( pool, sizeof( blk_descr_file_t ), _blk_descr_file_allocate, (void*)rangelist );
	if (NULL == blk_descr){
		log_err( "Failed to allocate block descriptor" );
		return -ENOMEM;
	}

	return SUCCESS;
}

blk_descr_file_t* blk_descr_file_pool_take( blk_descr_pool_t* pool )
{
	blk_descr_file_t* blk_descriptor = (blk_descr_file_t*)blk_descr_pool_take( pool,
		sizeof( blk_descr_file_t ) );

	INIT_LIST_HEAD( &blk_descriptor->rangelist );
	return blk_descriptor;
}

