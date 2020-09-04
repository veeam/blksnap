#pragma once
#include "container_spinlocking.h"
#include "tracker_queue.h"
#include "sparse_bitmap.h"
#include "rangelist.h"
#include "page_array.h"

typedef struct snapdata_collector_s
{
    content_sl_t content;

    dev_t dev_id;
    struct block_device* device;

    tracker_queue_t* tracker_queue;

    void* magic_buff;
    size_t magic_size;
#ifdef SNAPDATA_SPARSE_CHANGES
    sparse_bitmap_t changes_sparse;
#else
    page_array_t* changes;
    sector_t start_index;
    sector_t length;
#endif
    stream_size_t collected_size;
    stream_size_t already_set_size;
    /*
    sector_t collected_begin;
    sector_t collected_end;
    sector_t already_set_begin;
    sector_t already_set_end;
    */
    int fail_code;

    struct mutex locker;
}snapdata_collector_t;


int snapdata_collect_Init( void );
int snapdata_collect_Done( void );

int snapdata_collect_LocationStart( dev_t dev_id, void* MagicUserBuff, size_t MagicLength );
int snapdata_collect_LocationGet( dev_t dev_id, rangelist_t* rangelist, size_t* ranges_count );
int snapdata_collect_LocationComplete( dev_t dev_id );

int snapdata_collect_Get( dev_t dev_id, snapdata_collector_t** p_collector );

int snapdata_collect_Find(struct bio *bio, snapdata_collector_t** p_collector);

void snapdata_collect_Process( snapdata_collector_t* collector, struct bio *bio );

