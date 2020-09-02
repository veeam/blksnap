#pragma once
#include "queue_spinlocking.h"
#include "rangevector.h"
#include "blk_descr_unify.h"

int  blk_redirect_bioset_create( void );
void blk_redirect_bioset_free( void );


#if LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
void blk_redirect_bio_endio( struct bio *bb, int err );
#else
void blk_redirect_bio_endio( struct bio *bb );
#endif

typedef struct blk_redirect_bio_endio_list_s{
    struct blk_redirect_bio_endio_list_s* next;
    struct bio* this;
}blk_redirect_bio_endio_list_t;

typedef void (redirect_bio_endio_complete_cb)( void* complete_param, struct bio* rq, int err );

typedef struct redirect_bio_endio_s{
    queue_content_sl_t content;

    struct bio *bio;
    int err;
    blk_redirect_bio_endio_list_t* bio_endio_head_rec; //list of created bios
    atomic64_t bio_endio_count;

    void* complete_param;
    redirect_bio_endio_complete_cb* complete_cb;
}blk_redirect_bio_endio_t;


int blk_dev_redirect_part( blk_redirect_bio_endio_t* rq_endio, int direction, struct block_device*  blk_dev, sector_t target_pos, sector_t rq_ofs, sector_t rq_count );
void blk_dev_redirect_submit( blk_redirect_bio_endio_t* rq_endio );

int blk_dev_redirect_memcpy_part( blk_redirect_bio_endio_t* rq_endio, int direction, void* src_buff, sector_t rq_ofs, sector_t rq_count );
int blk_dev_redirect_zeroed_part( blk_redirect_bio_endio_t* rq_endio, sector_t rq_ofs, sector_t rq_count );

#ifdef SNAPDATA_ZEROED
int blk_dev_redirect_read_zeroed( blk_redirect_bio_endio_t* rq_endio, struct block_device* blk_dev, sector_t rq_pos, sector_t blk_ofs_start, sector_t blk_ofs_count, rangevector_t* zero_sectors );
#endif

void blk_redirect_complete( blk_redirect_bio_endio_t* rq_endio, int res );
