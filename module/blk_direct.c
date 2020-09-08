#include "common.h"
#include "blk_direct.h"

#define SECTION "blk       "
#include "log_format.h"

struct bio_set g_BlkDirectBioset = { 0 };
#define BlkDirectBioset &g_BlkDirectBioset

typedef struct blk_direct_bio_complete_s { // like struct submit_bio_ret
    struct completion event;
    int error;
}blk_direct_bio_complete_t;

int blk_direct_bioset_create( void )
{
    return bioset_init(BlkDirectBioset, 64, sizeof(blk_direct_bio_complete_t), BIOSET_NEED_BVECS | BIOSET_NEED_RESCUER);
}

void blk_direct_bioset_free( void )
{
    bioset_exit(BlkDirectBioset);
}

void blk_direct_bio_endio( struct bio *bb )
{
    if (bb->bi_private){
        blk_direct_bio_complete_t* bio_compl = (blk_direct_bio_complete_t*)(bb->bi_private);

        if (bb->bi_status != BLK_STS_OK)
            bio_compl->error = -EIO;
        else
            bio_compl->error = SUCCESS;

        complete( &(bio_compl->event) );
    }
}

struct bio* _blk_dev_direct_bio_alloc( int nr_iovecs )
{
    struct bio* new_bio = bio_alloc_bioset( GFP_NOIO, nr_iovecs, BlkDirectBioset );
    if (new_bio == NULL)
        return NULL;

    {
        blk_direct_bio_complete_t* bio_compl = (blk_direct_bio_complete_t*)((void*)new_bio - sizeof( blk_direct_bio_complete_t ));
        bio_compl->error = -EINVAL;
        init_completion( &bio_compl->event );
        new_bio->bi_private = bio_compl;
        new_bio->bi_end_io = blk_direct_bio_endio;
    }
    return new_bio;
}


int _dev_direct_submit_pages(
    struct block_device* blkdev,
    int direction,
    sector_t arr_ofs,
    page_array_t* arr,
    sector_t ofs_sector,
    sector_t size_sector,
    sector_t* processed_sectors
){
    blk_direct_bio_complete_t* bio_compl = NULL;
    struct bio *bb = NULL;
    int nr_iovecs;
    int page_inx = arr_ofs / (PAGE_SIZE / SECTOR_SIZE);
    sector_t process_sect = 0;

    {
        struct request_queue *q = bdev_get_queue( blkdev );
        size_sector = min_t( sector_t, size_sector, q->limits.max_sectors );

        size_sector = min_t( sector_t, size_sector, (BIO_MAX_PAGES << (PAGE_SHIFT - SECTOR_SHIFT)) );
    }

    nr_iovecs = page_count_calc_sectors( ofs_sector, size_sector );

    while (NULL == (bb = _blk_dev_direct_bio_alloc( nr_iovecs ))){
        log_err_d( "Failed to allocate pages for direct IO. nr_iovecs=", nr_iovecs );
        log_err_sect( "ofs_sector=", ofs_sector );
        log_err_sect( "size_sector=", size_sector );

        *processed_sectors = 0;
        return -ENOMEM;
    }
    bio_compl = (blk_direct_bio_complete_t*)bb->bi_private;

    bio_set_dev(bb, blkdev);

    if (direction == READ)
        bio_set_op_attrs( bb, REQ_OP_READ, 0 );
    else
        bio_set_op_attrs( bb, REQ_OP_WRITE, 0 );

    bb->bi_iter.bi_sector = ofs_sector;

    {
        sector_t unordered = arr_ofs & ((PAGE_SIZE / SECTOR_SIZE) - 1);
        sector_t bvec_len_sect = min_t( sector_t, ((PAGE_SIZE / SECTOR_SIZE) - unordered), size_sector );

        if (0 == bio_add_page( bb, arr->pg[page_inx].page, sector_to_uint( bvec_len_sect ), sector_to_uint( unordered ) ))
            goto blk_dev_direct_submit_pages_label_failed;

        ++page_inx;
        process_sect += bvec_len_sect;
    }
    while ((process_sect < size_sector) && (page_inx < arr->pg_cnt))
    {
        sector_t bvec_len_sect = min_t( sector_t, (PAGE_SIZE / SECTOR_SIZE), (size_sector - process_sect) );

        if (0 == bio_add_page( bb, arr->pg[page_inx].page, sector_to_uint( bvec_len_sect ), 0 )){
            break;
        }
        ++page_inx;
        process_sect += bvec_len_sect;
    }

    submit_bio( bb );

    wait_for_completion_io( &bio_compl->event );
    if (bio_compl->error != SUCCESS){
        log_err_d( "Failed to submit direct IO. errno=", bio_compl->error );
        process_sect = 0;
    }
    
blk_dev_direct_submit_pages_label_failed:
    bio_put( bb );

    *processed_sectors = process_sect;
    return SUCCESS;
}

sector_t blk_direct_submit_pages(
struct block_device* blkdev,
    int direction,
    sector_t arr_ofs,
    page_array_t* arr,
    sector_t ofs_sector,
    sector_t size_sector
    ){
    sector_t process_sect = 0;
    do{
        int result;
        sector_t portion = 0;

        result = _dev_direct_submit_pages( blkdev, direction, arr_ofs + process_sect, arr, ofs_sector + process_sect, size_sector - process_sect, &portion );
        if (SUCCESS != result)
            break;

        process_sect += portion;

    } while (process_sect < size_sector);

    return process_sect;
}


int blk_direct_submit_page( struct block_device* blkdev, int direction, sector_t ofs_sect, struct page* pg )
{
    int res = -EIO;
    blk_direct_bio_complete_t* bio_compl = NULL;
    struct bio *bb = _blk_dev_direct_bio_alloc(1);
    if (bb == NULL){
        log_err( "Failed to allocate bio for direct IO." );
        return -ENOMEM;
    }
    bio_compl = (blk_direct_bio_complete_t*)bb->bi_private;

    BUG_ON(blkdev == NULL);

    bio_set_dev(bb, blkdev);

    if (direction == READ)
        bio_set_op_attrs( bb, REQ_OP_READ, 0 );
    else if (direction == WRITE)
        bio_set_op_attrs(bb, REQ_OP_WRITE, 0);
    else if (direction == READ_SYNC)
        bio_set_op_attrs(bb, REQ_OP_READ, REQ_SYNC);
    else if (direction == WRITE_SYNC)
        bio_set_op_attrs(bb, REQ_OP_WRITE, REQ_SYNC );
    else{
        log_err("Invalid direction parameter");
        return -EINVAL;
    }

    bb->bi_iter.bi_sector = ofs_sect;

    BUG_ON(pg == NULL);
    if (0 != bio_add_page( bb, pg, PAGE_SIZE, 0 )){
        submit_bio( bb );

        wait_for_completion_io( &bio_compl->event );

        res = bio_compl->error;
        if (bio_compl->error != SUCCESS){
            log_err_d( "Failed to submit direct IO. errno=", bio_compl->error );
        }
    }
    else{
        log_err( "Failed to add page to direct IO bio" );
        res = -ENOMEM;
    }
    bio_put( bb );
    return res;
}

