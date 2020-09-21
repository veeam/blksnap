#pragma once
#include "sector.h"
#include "blk_descr_file.h"
#include "blk_descr_mem.h"
#include "blk_descr_multidev.h"

#define DEFER_IO_DIO_REQUEST_LENGTH 250
#define DEFER_IO_DIO_REQUEST_SECTORS_COUNT (10*1024*1024/SECTOR_SIZE)

typedef struct blk_deferred_s
{
	struct list_head link;

	unsigned long blk_index;
	union blk_descr_unify blk_descr;

	struct blk_range sect;

	struct page **page_array; //null pointer on tail
}blk_deferred_t;

typedef struct blk_deferred_request_s
{
	struct completion complete;
	sector_t sect_len;
	atomic64_t sect_processed;
	int result;

	struct list_head dios;
}blk_deferred_request_t;


void blk_deferred_done( void );

blk_deferred_t* blk_deferred_alloc( unsigned long block_index, union blk_descr_unify blk_descr );
void blk_deferred_free( blk_deferred_t* dio );

void blk_deferred_bio_endio( struct bio *bio );

void blk_deferred_complete( blk_deferred_request_t* dio_req, sector_t portion_sect_cnt, int result );

sector_t blk_deferred_submit_pages( struct block_device* blk_dev, blk_deferred_request_t* dio_req, int direction, sector_t arr_ofs, struct page **page_array, sector_t ofs_sector, sector_t size_sector );

void blk_deferred_memcpy_read( char* databuff, blk_deferred_request_t* dio_req, struct page **page_array, sector_t arr_ofs, sector_t size_sector );


blk_deferred_request_t* blk_deferred_request_new( void );

bool blk_deferred_request_already_added( blk_deferred_request_t* dio_req, unsigned long block_index );

int  blk_deferred_request_add( blk_deferred_request_t* dio_req, blk_deferred_t* dio );
void blk_deferred_request_free( blk_deferred_request_t* dio_req );
void blk_deferred_request_deadlocked( blk_deferred_request_t* dio_req );

void blk_deferred_request_waiting_skip( blk_deferred_request_t* dio_req );
int blk_deferred_request_wait( blk_deferred_request_t* dio_req );

int blk_deferred_bioset_create( void );
void blk_deferred_bioset_free( void );

int blk_deferred_request_read_original( struct block_device*  original_blk_dev, blk_deferred_request_t* dio_copy_req );

int blk_deferred_request_store_file( struct block_device*  blk_dev, blk_deferred_request_t* dio_copy_req );
#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
int blk_deferred_request_store_multidev( blk_deferred_request_t* dio_copy_req );
#endif
int blk_deffered_request_store_mem( blk_deferred_request_t* dio_copy_req );

