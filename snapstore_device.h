#pragma once

#include "container.h"
#include "container_spinlocking.h"
#include "rangevector.h"
#include "blk_deferred.h"
#include "blk_redirect.h"
#include "snapstore.h"


typedef struct snapstore_copy_block_request_s
{
    content_sl_t content;
    blk_descr_array_index_t block_index;
    sector_t dst_ofs;
}snapstore_copy_block_request_t;


typedef struct snapstore_device_s
{
    content_t content;
    shared_resource_t shared;
    dev_t dev_id;
    snapstore_t* snapstore;

    struct block_device* orig_blk_dev;


    blk_descr_array_t store_block_map; // map block index to read block offset
    //struct rw_semaphore store_block_map_locker;
    struct mutex store_block_map_locker;

#ifdef SNAPDATA_ZEROED
    rangevector_t zero_sectors;
#endif

    // transaction processing
    //rangevector_t copy_ranges;
    //container_sl_t copy_requests;

    atomic_t req_failed_cnt;
    volatile int err_code;
    volatile bool corrupted;
}snapstore_device_t;

int snapstore_device_init( void );
void snapstore_device_done( void );

static inline snapstore_device_t* snapstore_device_get_resource( snapstore_device_t* snapstore_device )
{
    return (snapstore_device_t*)shared_resource_get( &snapstore_device->shared );
};
static inline void snapstore_device_put_resource( snapstore_device_t* snapstore_device )
{
    shared_resource_put( &snapstore_device->shared );
};
snapstore_device_t* snapstore_device_find_by_dev_id( dev_t dev_id );

int snapstore_device_create( dev_t dev_id, snapstore_t* snapstore );


int snapstore_device_cleanup( veeam_uuid_t* id );

int snapstore_device_prepare_requests( snapstore_device_t* snapstore_device, range_t* copy_range, blk_deferred_request_t** dio_copy_req );
int snapstore_device_store( snapstore_device_t* snapstore_device, blk_deferred_request_t* dio_copy_req );

int snapstore_device_read( snapstore_device_t* snapstore_device, blk_redirect_bio_endio_t* rq_endio ); //request from image
int snapstore_device_write( snapstore_device_t* snapstore_device, blk_redirect_bio_endio_t* rq_endio ); //request from image

bool snapstore_device_is_corrupted( snapstore_device_t* snapstore_device );
void snapstore_device_set_corrupted( snapstore_device_t* snapstore_device, int err_code );
int snapstore_device_errno( dev_t dev_id, int* p_err_code );

void snapstore_device_print_state( snapstore_device_t* snapstore_device );



static inline void _snapstore_device_descr_read_lock( snapstore_device_t* snapstore_device )
{
    //down_read( &snapstore_device->store_block_map_locker );
    mutex_lock( &snapstore_device->store_block_map_locker );
}
static inline void _snapstore_device_descr_read_unlock( snapstore_device_t* snapstore_device )
{
    //up_read( &snapstore_device->store_block_map_locker );
    mutex_unlock( &snapstore_device->store_block_map_locker );
}
