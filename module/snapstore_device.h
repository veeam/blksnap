#pragma once

#include "rangevector.h"
#include "blk_deferred.h"
#include "blk_redirect.h"
#include "snapstore.h"
#include <linux/xarray.h>
#include <linux/kref.h>

typedef struct snapstore_device_s
{
	struct list_head link;
	struct kref refcount;

	dev_t dev_id;
	snapstore_t* snapstore;

	struct block_device* orig_blk_dev;

	struct xarray store_block_map;	// map block index to read block offset
	struct mutex store_block_map_locker;

	rangevector_t zero_sectors;

	atomic_t req_failed_cnt;
	volatile int err_code;
	volatile bool corrupted;
}snapstore_device_t;

void snapstore_device_done( void );

snapstore_device_t* snapstore_device_get_resource( snapstore_device_t* snapstore_device );
void snapstore_device_put_resource( snapstore_device_t* snapstore_device );

snapstore_device_t* snapstore_device_find_by_dev_id( dev_t dev_id );

int snapstore_device_create( dev_t dev_id, snapstore_t* snapstore );

int snapstore_device_cleanup( uuid_t* id );

int snapstore_device_prepare_requests( snapstore_device_t* snapstore_device, struct blk_range* copy_range, blk_deferred_request_t** dio_copy_req );
int snapstore_device_store( snapstore_device_t* snapstore_device, blk_deferred_request_t* dio_copy_req );

int snapstore_device_read( snapstore_device_t* snapstore_device, blk_redirect_bio_t* rq_redir ); //request from image
int snapstore_device_write( snapstore_device_t* snapstore_device, blk_redirect_bio_t* rq_redir ); //request from image

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
