#pragma once
#include "blk_util.h"
#include "page_array.h"

int  blk_direct_bioset_create( void );
void blk_direct_bioset_free( void );

void blk_direct_bio_endio( struct bio *bb );

#ifndef READ_SYNC
#define READ_SYNC		(READ | REQ_SYNC)
#endif

#ifndef WRITE_SYNC
#define WRITE_SYNC		(WRITE | REQ_SYNC)
#endif

sector_t blk_direct_submit_pages( struct block_device* blkdev, int direction, sector_t arr_ofs, page_array_t* arr, sector_t ofs_sector, sector_t size_sector );
int blk_direct_submit_page( struct block_device* blkdev, int direction, sector_t ofs_sect, struct page* pg );

