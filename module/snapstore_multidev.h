#pragma once

#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV

#include "blk_deferred.h"
#include "blk_descr_multidev.h"
#include "container_spinlocking.h"

typedef struct multidev_el_s
{
	struct list_head link;

	dev_t dev_id;
	struct block_device* blk_dev;

}multidev_el_t;

typedef struct snapstore_multidev_s
{
	struct list_head devicelist; //for mapping device id to opened device struct pointer
	spinlock_t devicelist_lock;

	blk_descr_pool_t pool;
}snapstore_multidev_t;

int snapstore_multidev_create( snapstore_multidev_t** p_file );

void snapstore_multidev_destroy( snapstore_multidev_t* file );

struct block_device* snapstore_multidev_get_device( snapstore_multidev_t* multidev, dev_t dev_id );
#endif
