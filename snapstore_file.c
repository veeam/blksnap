#include "stdafx.h"
#include "snapstore_file.h"
#include "blk_util.h"

#define SECTION "snapstore "
#include "log_format.h"

int snapstore_file_create( dev_t dev_id, snapstore_file_t** pfile )
{
    int res = SUCCESS;
    snapstore_file_t* file;

    log_tr_dev_t("Single device file snapstore was created on device ", dev_id);

    file = dbg_kzalloc( sizeof( snapstore_file_t ), GFP_KERNEL );
    if (file == NULL)
        return -ENOMEM;

    res = blk_dev_open( dev_id, &file->blk_dev );
    if (res != SUCCESS){
        dbg_kfree( file );
        log_err_format( "Unable to create snapstore file: failed to open device [%d:%d]. errno=", MAJOR( dev_id ), MINOR( dev_id ), res );
        return res;
    }
    {
        struct request_queue *q = bdev_get_queue(file->blk_dev);

        log_tr_d("snapstore device logical block size ", q->limits.logical_block_size);
        log_tr_d("snapstore device physical block size ", q->limits.physical_block_size);
    }

    file->blk_dev_id = dev_id;
    blk_descr_file_pool_init( &file->pool );

    *pfile = file;
    return res;
}

void snapstore_file_destroy( snapstore_file_t* file )
{
    if (file){
        blk_descr_file_pool_done( &file->pool );

        if (file->blk_dev != NULL){
            blk_dev_close( file->blk_dev );
            file->blk_dev = NULL;
        }

        dbg_kfree(file);
    }
}

