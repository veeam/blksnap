#pragma once

#include "sector.h"

typedef struct blk_dev_info_s
{
    size_t blk_size;
    sector_t start_sect;
    sector_t count_sect;

    unsigned int io_min;
    unsigned int physical_block_size;
    unsigned short logical_block_size;

}blk_dev_info_t;
int blk_dev_get_info( dev_t dev_id, blk_dev_info_t* pdev_info );

int blk_dev_open( dev_t dev_id, struct block_device** p_blk_dev );
void blk_dev_close( struct block_device* blk_dev );

int blk_freeze_bdev( dev_t dev_id, struct block_device* device, struct super_block** psuperblock );
struct super_block* blk_thaw_bdev( dev_t dev_id, struct block_device* device, struct super_block* superblock );


static inline 
sector_t blk_dev_get_capacity( struct block_device* blk_dev )
{
    return blk_dev->bd_part->nr_sects;
};
