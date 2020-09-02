#pragma once

typedef struct blk_descr_unify_s
{
    struct list_head link;
    //size_t blk_index;
}blk_descr_unify_t;

static inline void blk_descr_unify_init( blk_descr_unify_t* blk_descr )
{
    INIT_LIST_HEAD( &blk_descr->link );
}
