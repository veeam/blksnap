#include "stdafx.h"
#ifdef SNAPSTORE_MULTIDEV

#include "snapstore_multidev.h"
#include "blk_util.h"

#define SECTION "snapstore "
#include "log_format.h"

int snapstore_multidev_create( snapstore_multidev_t** p_multidev )
{
    int res = SUCCESS;
    snapstore_multidev_t* multidev;

    log_tr( "Multidevice file snapstore create" );

    multidev = dbg_kzalloc( sizeof( snapstore_multidev_t ), GFP_KERNEL );
    if (multidev == NULL)
        return -ENOMEM;

    container_sl_init( &multidev->devicelist, sizeof(multidev_el_t) );

    blk_descr_multidev_pool_init( &multidev->pool );

    *p_multidev = multidev;
    return res;
}

void snapstore_multidev_destroy( snapstore_multidev_t* multidev )
{
    if (multidev){
        blk_descr_multidev_pool_done( &multidev->pool );

        {
            content_sl_t* content;
            while (NULL != (content = container_sl_get_first( &multidev->devicelist )))
            {
                multidev_el_t* el = (multidev_el_t*)(content);
                blk_dev_close( el->blk_dev );
                log_tr_dev_t( "Close device for multidevice snapstore ", el->dev_id);
            }
            content_sl_free( content );
        }

        dbg_kfree( multidev );
    }
}

struct block_device* snapstore_multidev_get_device( snapstore_multidev_t* multidev, dev_t dev_id )
{
    struct block_device* blk_dev = NULL;
    content_sl_t* content = NULL;
    CONTAINER_SL_FOREACH_BEGIN( multidev->devicelist, content )
    {
        multidev_el_t* el = (multidev_el_t*)(content);

        if (el->dev_id == dev_id){
            blk_dev = el->blk_dev;
            break;
        }
    }CONTAINER_SL_FOREACH_END( multidev->devicelist );

    if (NULL == blk_dev){
        int res = blk_dev_open( dev_id, &blk_dev );
        if (res != SUCCESS){
            blk_dev = NULL;
            log_err_format( "Unable to add device to snapstore multidevice file: failed to open [%d:%d]. errno=", MAJOR( dev_id ), MINOR( dev_id ), res );
        }
        {//push opened device to container
            multidev_el_t* content = (multidev_el_t*)content_sl_new(&multidev->devicelist);
            content->blk_dev = blk_dev;
            content->dev_id = dev_id;

            container_sl_push_back(&multidev->devicelist, &content->content);
        }
        {//logging
            struct request_queue *q = bdev_get_queue(blk_dev);
            log_tr_dev_t("Open device for multidevice snapstore ", dev_id);
            log_tr_d("    logical block size ", q->limits.logical_block_size);
            log_tr_d("    physical block size ", q->limits.physical_block_size);
        }
    }
    return blk_dev;
}

#endif