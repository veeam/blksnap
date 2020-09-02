#pragma once
#include "page_array.h"
#include "range.h"
#include "container.h"
#include "blk_descr_file.h"
#include "blk_descr_mem.h"
#include "blk_descr_multidev.h"
#include "blk_descr_array.h"


#define BLK_DEFER_LIST

typedef struct blk_deferred_s
{
#ifdef BLK_DEFER_LIST
    struct list_head link;
#endif

    blk_descr_array_index_t blk_index; //for writing to snapstore
    blk_descr_unify_t* blk_descr;    //for writing to snapstore - blk_descr_file_t or blk_descr_mem_t

    range_t sect;

    page_array_t* buff;
}blk_deferred_t;

typedef struct blk_deferred_request_s
{
    struct completion complete;
    sector_t sect_len;
    atomic64_t sect_processed;
    int result;

#ifdef BLK_DEFER_LIST
    struct list_head dios;
#else
    int dios_cnt;
    blk_deferred_t* dios[DEFER_IO_DIO_REQUEST_LENGTH];
#endif

}blk_deferred_request_t;


void blk_deferred_init( void );
void blk_deferred_done( void );
void blk_deferred_print_state( void );

blk_deferred_t* blk_deferred_alloc( blk_descr_array_index_t block_index, blk_descr_unify_t* blk_descr );
void blk_deferred_free( blk_deferred_t* dio );

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
void blk_deferred_bio_endio( struct bio *bio, int err );
#else
void blk_deferred_bio_endio( struct bio *bio );
#endif

void blk_deferred_complete( blk_deferred_request_t* dio_req, sector_t portion_sect_cnt, int result );

sector_t blk_deferred_submit_pages( struct block_device* blk_dev, blk_deferred_request_t* dio_req, int direction, sector_t arr_ofs, page_array_t* arr, sector_t ofs_sector, sector_t size_sector );

void blk_deferred_memcpy_read( char* databuff, blk_deferred_request_t* dio_req, page_array_t* arr, sector_t arr_ofs, sector_t size_sector );


blk_deferred_request_t* blk_deferred_request_new( void );

bool blk_deferred_request_already_added( blk_deferred_request_t* dio_req, blk_descr_array_index_t block_index );

int  blk_deferred_request_add( blk_deferred_request_t* dio_req, blk_deferred_t* dio );
void blk_deferred_request_free( blk_deferred_request_t* dio_req );
void blk_deferred_request_deadlocked( blk_deferred_request_t* dio_req );

void blk_deferred_request_waiting_skip( blk_deferred_request_t* dio_req );
int blk_deferred_request_wait( blk_deferred_request_t* dio_req );

int blk_deferred_bioset_create( void );
void blk_deferred_bioset_free( void );

int blk_deferred_request_read_original( struct block_device*  original_blk_dev, blk_deferred_request_t* dio_copy_req );

int blk_deferred_request_store_file( struct block_device*  blk_dev, blk_deferred_request_t* dio_copy_req );
#ifdef SNAPSTORE_MULTIDEV
int blk_deferred_request_store_multidev( blk_deferred_request_t* dio_copy_req );
#endif
int blk_deffered_request_store_mem( blk_deferred_request_t* dio_copy_req );

