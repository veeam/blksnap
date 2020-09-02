#pragma once
#include "blk_descr_unify.h"


typedef size_t blk_descr_array_index_t;
typedef blk_descr_unify_t* blk_descr_array_el_t;

#define BLK_DESCR_GROUP_LENGTH_SHIFT 8
#define BLK_DESCR_GROUP_LENGTH (1 << BLK_DESCR_GROUP_LENGTH_SHIFT)
#define BLK_DESCR_GROUP_LENGTH_MASK (BLK_DESCR_GROUP_LENGTH-1)


typedef struct blk_descr_array_group_s
{
    size_t cnt;
    unsigned char bitmap[BLK_DESCR_GROUP_LENGTH >> 3];
    blk_descr_array_el_t values[BLK_DESCR_GROUP_LENGTH];
}blk_descr_array_group_t;

typedef struct blk_descr_array_s
{
    blk_descr_array_index_t first;
    blk_descr_array_index_t last;
    page_array_t* groups;
    size_t group_count;

    struct rw_semaphore rw_lock;
}blk_descr_array_t;

int blk_descr_array_init( blk_descr_array_t* header, blk_descr_array_index_t first, blk_descr_array_index_t last );

void blk_descr_array_done( blk_descr_array_t* header );

void blk_descr_array_reset( blk_descr_array_t* header );

int blk_descr_array_set( blk_descr_array_t* header, blk_descr_array_index_t inx, blk_descr_array_el_t value );

int blk_descr_array_get( blk_descr_array_t* header, blk_descr_array_index_t inx, blk_descr_array_el_t* p_value );
