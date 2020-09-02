#pragma once
#include "range.h"
#include "rangelist.h"
#include "blk_descr_unify.h"
#include "blk_descr_pool.h"

typedef struct blk_descr_file_s
{
    blk_descr_unify_t unify;

    rangelist_t rangelist;
}blk_descr_file_t;


void blk_descr_file_pool_init( blk_descr_pool_t* pool );
void blk_descr_file_pool_done( blk_descr_pool_t* pool );


int blk_descr_file_pool_add( blk_descr_pool_t* pool, rangelist_t* rangelist ); //allocate new empty block
blk_descr_file_t* blk_descr_file_pool_take( blk_descr_pool_t* pool ); //take empty
