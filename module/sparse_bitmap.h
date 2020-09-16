#pragma once

#include "sector.h"

#if (BITS_PER_LONG == 64)
#define SPARSE_BITMAP_BLOCK_SIZE_DEGREE	6ull	//block have 64 elements
#endif

#if (BITS_PER_LONG == 32)
#define SPARSE_BITMAP_BLOCK_SIZE_DEGREE	5ull	//block have 32 elements
#endif

#define SPARSE_BITMAP_BLOCK_SIZE			((size_t)1<<SPARSE_BITMAP_BLOCK_SIZE_DEGREE)
#define SPARSE_BITMAP_BLOCK_SIZE_MASK		(SPARSE_BITMAP_BLOCK_SIZE-1)


#define BLOCK_EMPTY NULL
#define BLOCK_FULL  (void*)(-1)

typedef struct blocks_array_s{
	void* blk[SPARSE_BITMAP_BLOCK_SIZE];
}blocks_array_t;

typedef struct sparse_block_s{
	union{
		blocks_array_t* blocks_array;
		size_t bit_block;
	};
	char level;
	char fill_count;
	char cnt_full;
}sparse_block_t;

typedef struct sparse_bitmap_s{
	u64	start_index;
	u64	length;
	sparse_block_t	sparse_block;
}sparse_bitmap_t;

int  sparsebitmap_init( void );
void sparsebitmap_done( void );

void sparsebitmap_create( sparse_bitmap_t* bitmap, u64 min_index, u64 length );
void sparsebitmap_destroy( sparse_bitmap_t* bitmap );

int sparsebitmap_Set( sparse_bitmap_t* bitmap, u64 index, bool state );
int sparsebitmap_Get( sparse_bitmap_t* bitmap, u64 index, bool* p_state );
