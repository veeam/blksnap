#pragma once

struct blk_descr_mem;
struct blk_descr_file;
struct blk_descr_multidev;

union blk_descr_unify
{
	void* ptr;
	struct blk_descr_mem* mem;
	struct blk_descr_file* file;
	struct blk_descr_multidev* multidev;
};

typedef struct blk_descr_pool_s
{
	struct list_head head;
	struct mutex lock;

	size_t blocks_cnt; //count of pool_el_t

	volatile size_t total_cnt; ///total count of block descriptors
	volatile size_t take_cnt; // take count of block descriptors
}blk_descr_pool_t;

typedef struct  pool_el_s
{
	struct list_head link;

	size_t used_cnt; // used blocks
	size_t capacity; // blocks array capacity

	u8 descr_array[0];
}pool_el_t;

void blk_descr_pool_init( blk_descr_pool_t* pool, size_t available_blocks);

typedef void( *blk_descr_cleanup_t )(void *descr_array, size_t count);
void blk_descr_pool_done( blk_descr_pool_t* pool, blk_descr_cleanup_t blocks_cleanup );

typedef union blk_descr_unify (*blk_descr_allocate_cb)(void* descr_array, size_t index, void* arg);
union blk_descr_unify blk_descr_pool_alloc( blk_descr_pool_t* pool, size_t blk_descr_size, blk_descr_allocate_cb block_alloc, void* arg );

union blk_descr_unify blk_descr_pool_take( blk_descr_pool_t* pool, size_t blk_descr_size );

bool blk_descr_pool_check_halffill( blk_descr_pool_t* pool, sector_t empty_limit, sector_t* fill_status );
