#include "common.h"
#include "snapstore_mem.h"
#include "snapstore_blk.h"

typedef struct buffer_el_s{
	struct list_head link;
	void* buff;
}buffer_el_t;


snapstore_mem_t* snapstore_mem_create( size_t available_blocks )
{
	snapstore_mem_t* mem = kzalloc( sizeof( snapstore_mem_t ), GFP_KERNEL );

	if (NULL == mem)
		return NULL;

	blk_descr_mem_pool_init( &mem->pool, available_blocks );

	mem->blocks_limit = available_blocks;

	INIT_LIST_HEAD( &mem->blocks );
	mutex_init( &mem->blocks_lock );

	return mem;
}

void snapstore_mem_destroy( snapstore_mem_t* mem )
{
	buffer_el_t* buffer_el;

	if (NULL == mem)
		return ;

	do {
		buffer_el = NULL;

		mutex_lock( &mem->blocks_lock );
		if (!list_empty( &mem->blocks )) {
			buffer_el = list_entry( mem->blocks.next, buffer_el_t, link );

			list_del( &buffer_el->link );
		}
		mutex_unlock( &mem->blocks_lock );

		if (buffer_el) {
			vfree( buffer_el->buff );
			kfree( buffer_el);
		}
	}while(buffer_el);

	blk_descr_mem_pool_done( &mem->pool );

	kfree( mem );
}

void* snapstore_mem_get_block( snapstore_mem_t* mem )
{
	buffer_el_t* buffer_el;

	if (mem->blocks_allocated >= mem->blocks_limit){
		pr_err("Unable to get block from snapstore in memory\n");
		pr_err("Block limit is reached, allocated %ld, limit %ld\n",
			mem->blocks_allocated, mem->blocks_limit );
		return NULL;
	}

	buffer_el = kzalloc(sizeof(buffer_el_t), GFP_KERNEL);
	if (buffer_el == NULL)
		return NULL;
	INIT_LIST_HEAD( &buffer_el->link );

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,8,0)
	buffer_el->buff = __vmalloc(snapstore_block_size() * SECTOR_SIZE, GFP_NOIO);
#else
	buffer_el->buff = __vmalloc( snapstore_block_size() * SECTOR_SIZE, GFP_NOIO, PAGE_KERNEL );
#endif
	if (buffer_el->buff == NULL){
		kfree( buffer_el );
		return NULL;
	}

	++mem->blocks_allocated;
	if (0 == (mem->blocks_allocated & 0x7F))
		pr_info( "%ld MiB was allocated\n", mem->blocks_allocated );

	mutex_lock( &mem->blocks_lock );
	list_add_tail( &buffer_el->link, &mem->blocks);
	mutex_unlock( &mem->blocks_lock );

	return buffer_el->buff;
}
