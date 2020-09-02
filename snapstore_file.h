#pragma once

#include "blk_deferred.h"

typedef struct snapstore_file_s{
    dev_t blk_dev_id;
    struct block_device*  blk_dev;

    blk_descr_pool_t pool;
}snapstore_file_t;

int snapstore_file_create( dev_t dev_id, snapstore_file_t** pfile );

void snapstore_file_destroy( snapstore_file_t* file );


