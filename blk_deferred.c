#include "stdafx.h"
#include "blk_deferred.h"
#include "blk_util.h"
#include "queue_spinlocking.h"
#include "container_spinlocking.h"
#include "page_array.h"
#include "range.h"
#include "snapstore.h"

#define SECTION "blk       "
#include "log_format.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION( 4, 18, 0 )
struct bio_set* BlkDeferredBioset = NULL;
#else
struct bio_set g_BlkDeferredBioset = { 0 };
#define BlkDeferredBioset &g_BlkDeferredBioset
#endif

typedef struct dio_bio_complete_s{
    blk_deferred_request_t* dio_req;
    sector_t bio_sect_len;
}dio_bio_complete_t;

typedef struct dio_deadlocked_s
{
    content_sl_t content;
    blk_deferred_request_t* dio_req;
}dio_deadlocked_t;

static container_sl_t DioDeadlocked;

atomic64_t dio_alloc_count;
atomic64_t dio_free_count;

void blk_deferred_init( void )
{
    atomic64_set( &dio_alloc_count, 0 );
    atomic64_set( &dio_free_count, 0 );

    container_sl_init( &DioDeadlocked, sizeof( dio_deadlocked_t ) );
}
void blk_deferred_done( void )
{
    content_sl_t* content;
    while (NULL != (content = container_sl_get_first( &DioDeadlocked )) )
    {
        dio_deadlocked_t* dio_locked = (dio_deadlocked_t*)content;
        if (dio_locked->dio_req->sect_len == atomic64_read( &dio_locked->dio_req->sect_processed )){
            blk_deferred_request_free( dio_locked->dio_req );
        }
        else{
            log_err( "Locked defer IO is still in memory" );
        }
        content_sl_free( content );
    }

    container_sl_done( &DioDeadlocked );
}

void blk_deferred_print_state( void )
{
    log_warn( "" );
    log_warn( "Defer IO state:" );
    log_warn_lld( "Defer IO allocated: ", (long long int)atomic64_read( &dio_alloc_count ) );
    log_warn_lld( "Defer IO freed: ", (long long int)atomic64_read( &dio_free_count ) );
    log_warn_lld( "Defer IO in use: ", (long long int)atomic64_read( &dio_alloc_count ) - (long long int)atomic64_read( &dio_free_count ) );
}

void blk_deferred_request_deadlocked( blk_deferred_request_t* dio_req )
{
    dio_deadlocked_t* dio_locked = (dio_deadlocked_t*)content_sl_new( &DioDeadlocked );
    dio_locked->dio_req = dio_req;
    container_sl_push_back( &DioDeadlocked, &dio_locked->content );

    log_warn( "Deadlock with defer IO" );
}

void blk_deferred_free( blk_deferred_t* dio )
{
    if (dio->buff != NULL){
        page_array_free( dio->buff );
        dio->buff = NULL;
    }
    dbg_kfree( dio );
}

blk_deferred_t* blk_deferred_alloc( blk_descr_array_index_t block_index, blk_descr_unify_t* blk_descr )
{
    bool success = false;
    blk_deferred_t* dio = dbg_kmalloc( sizeof( blk_deferred_t ), GFP_NOIO );
    if (dio == NULL)
        return NULL;

#ifdef BLK_DEFER_LIST
    INIT_LIST_HEAD( &dio->link );
#endif

    dio->blk_descr = blk_descr;
    dio->blk_index = block_index;

    dio->sect.ofs = block_index << SNAPSTORE_BLK_SHIFT;
    dio->sect.cnt = SNAPSTORE_BLK_SIZE;

    do{
        int page_count = SNAPSTORE_BLK_SIZE / SECTORS_IN_PAGE;

        dio->buff = page_array_alloc( page_count, GFP_NOIO );
        if (dio->buff == NULL)
            break;

        success = true;
    } while (false);


    if (!success){
        log_err_format( "Failed to allocate defer IO block [%ld]", block_index );

        blk_deferred_free( dio );
        dio = NULL;
    }

    return dio;
}


int blk_deferred_bioset_create( void )
{
#if LINUX_VERSION_CODE < KERNEL_VERSION( 4, 18, 0 )
    BlkDeferredBioset = blk_bioset_create(sizeof(dio_bio_complete_t));
    if (BlkDeferredBioset == NULL){
        log_err( "Failed to create bio set for defer IO" );
        return -ENOMEM;
    }
    log_tr( "Bio set for defer IO create" );
    return SUCCESS;
#else
    return bioset_init(BlkDeferredBioset, 64, sizeof(dio_bio_complete_t), BIOSET_NEED_BVECS | BIOSET_NEED_RESCUER);
#endif
}

void blk_deferred_bioset_free( void )
{
#if LINUX_VERSION_CODE < KERNEL_VERSION( 4, 18, 0 )
    if (BlkDeferredBioset != NULL){
        bioset_free( BlkDeferredBioset );
        BlkDeferredBioset = NULL;

        log_tr( "Bio set for defer IO free" );
    }
#else
    bioset_exit(BlkDeferredBioset);
#endif
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
void blk_deferred_bio_free( struct bio* bio )
{
    bio_free( bio, BlkDeferredBioset );
}
#endif

struct bio* _blk_deferred_bio_alloc( int nr_iovecs )
{
    struct bio* new_bio = bio_alloc_bioset( GFP_NOIO, nr_iovecs, BlkDeferredBioset );
    if (new_bio){
        new_bio->bi_end_io = blk_deferred_bio_endio;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
        new_bio->bi_destructor = blk_deferred_bio_free;
#endif
        new_bio->bi_private = ((void*)new_bio) - sizeof( dio_bio_complete_t );
    }
    return new_bio;
}



void blk_deferred_complete( blk_deferred_request_t* dio_req, sector_t portion_sect_cnt, int result )
{
    atomic64_add( portion_sect_cnt, &dio_req->sect_processed );

    if (dio_req->sect_len == atomic64_read( &dio_req->sect_processed )){
        complete( &dio_req->complete );
    }

    if (result != SUCCESS){
        dio_req->result = result;
        log_err_d( "Failed to process defer IO request. errno=", result );
    }
}


#if LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
void blk_deferred_bio_endio( struct bio *bio, int err )
#else
void blk_deferred_bio_endio( struct bio *bio )
#endif
{
    int local_err;
    dio_bio_complete_t* complete_param = (dio_bio_complete_t*)bio->bi_private;

    if (complete_param == NULL){
//        WARN( true, "bio already end." );
    }else{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
        local_err = err;
#else

#ifndef BLK_STS_OK//#if LINUX_VERSION_CODE < KERNEL_VERSION( 4, 13, 0 )
        local_err = bio->bi_error;
#else
        if (bio->bi_status != BLK_STS_OK)
            local_err = -EIO;
        else
            local_err = SUCCESS;
#endif

#endif
        blk_deferred_complete( complete_param->dio_req, complete_param->bio_sect_len, local_err );
        bio->bi_private = NULL;
    }

    bio_put( bio );
}


sector_t _blk_deferred_submit_pages(
    struct block_device* blk_dev,
    blk_deferred_request_t* dio_req,
    int direction,
    sector_t arr_ofs,
    page_array_t* arr,
    sector_t ofs_sector,
    sector_t size_sector
){

    struct bio *bio = NULL;
    int nr_iovecs;
    int page_inx = arr_ofs >> (PAGE_SHIFT - SECTOR_SHIFT);
    sector_t process_sect = 0;

    nr_iovecs = page_count_calc_sectors( ofs_sector, size_sector );

    while (NULL == (bio = _blk_deferred_bio_alloc( nr_iovecs ))){
        //log_tr_d( "Failed to allocate bio for defer IO. nr_iovecs=", nr_iovecs );

        size_sector = (size_sector >> 1) & ~(SECTORS_IN_PAGE - 1);
        if (size_sector == 0){
            return 0;
        }
        nr_iovecs = page_count_calc_sectors( ofs_sector, size_sector );
    }

#ifdef bio_set_dev
    bio_set_dev(bio, blk_dev);
#else
    bio->bi_bdev = blk_dev;
#endif

#ifndef REQ_OP_BITS //#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
    bio->bi_rw = direction;
#else
    if (direction == READ)
        bio_set_op_attrs( bio, REQ_OP_READ, 0 );
    else
        bio_set_op_attrs( bio, REQ_OP_WRITE, 0 );
#endif
    bio_bi_sector( bio ) = ofs_sector;

    {//add first
        struct page* page;
        sector_t unordered = arr_ofs & (SECTORS_IN_PAGE - 1);
        sector_t bvec_len_sect = min_t( sector_t, (SECTORS_IN_PAGE - unordered), size_sector );

        BUG_ON( (page_inx > arr->pg_cnt) );
        page = arr->pg[page_inx].page;
        if (0 == bio_add_page( bio, page, sector_to_uint( bvec_len_sect ), sector_to_uint( unordered ) )){
            //log_err_d( "bvec full! bi_size=", bio_bi_size( bio ) );
            bio_put( bio );
            return 0;
        }
        ++page_inx;
        process_sect += bvec_len_sect;
    }

    while ((process_sect < size_sector) && (page_inx < arr->pg_cnt))
    {
        struct page* page;
        sector_t bvec_len_sect = min_t( sector_t, SECTORS_IN_PAGE, (size_sector - process_sect) );

        BUG_ON( (page_inx > arr->pg_cnt));
        page = arr->pg[page_inx].page;
        if (0 == bio_add_page( bio, page, sector_to_uint( bvec_len_sect ), 0 ))
            break;

        ++page_inx;
        process_sect += bvec_len_sect;
    }


    ((dio_bio_complete_t*)bio->bi_private)->dio_req = dio_req;
    ((dio_bio_complete_t*)bio->bi_private)->bio_sect_len = process_sect;

#ifndef REQ_OP_BITS //#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
    submit_bio( direction, bio );
#else
    submit_bio( bio );
#endif

    return process_sect;
}


sector_t blk_deferred_submit_pages(
    struct block_device* blk_dev,
    blk_deferred_request_t* dio_req,
    int direction,
    sector_t arr_ofs,
    page_array_t* arr,
    sector_t ofs_sector,
    sector_t size_sector
){
    sector_t process_sect = 0;

    do{
        sector_t portion_sect = _blk_deferred_submit_pages( blk_dev, dio_req, direction, arr_ofs + process_sect, arr, ofs_sector + process_sect, size_sector - process_sect );
        if (portion_sect == 0){
            log_err_format( "Failed to submit defer IO pages. Only [%lld] sectors processed", process_sect );
            break;
        }
        process_sect += portion_sect;
    } while (process_sect < size_sector);

    return process_sect;
}


void blk_deferred_memcpy_read( char* databuff, blk_deferred_request_t* dio_req, page_array_t* arr, sector_t arr_ofs, sector_t size_sector )
{
    sector_t sect_inx;
    for (sect_inx = 0; sect_inx < size_sector; ++sect_inx){
        memcpy( page_get_sector( arr, sect_inx + arr_ofs ), databuff + (sect_inx<<SECTOR_SHIFT), SECTOR_SIZE );
    }
    blk_deferred_complete( dio_req, size_sector, SUCCESS );
}

blk_deferred_request_t* blk_deferred_request_new( void )
{
    blk_deferred_request_t* dio_req = NULL;

    dio_req = dbg_kzalloc( sizeof( blk_deferred_request_t ), GFP_NOIO );
    if (dio_req == NULL)
        return NULL;

#ifdef BLK_DEFER_LIST
    INIT_LIST_HEAD( &dio_req->dios );
#else
    dio_req->dios_cnt = 0;
#endif

    dio_req->result = SUCCESS;
    atomic64_set( &dio_req->sect_processed, 0 );
    dio_req->sect_len = 0;
    init_completion( &dio_req->complete );

    return dio_req;
}

#ifdef BLK_DEFER_LIST
bool blk_deferred_request_already_added( blk_deferred_request_t* dio_req, blk_descr_array_index_t block_index )
{
    bool result = false;
    if (!list_empty( &dio_req->dios )){
        struct list_head* _list_head;
        list_for_each( _list_head, &dio_req->dios ){
            blk_deferred_t* dio = list_entry( _list_head, blk_deferred_t, link );

            if (dio->blk_index == block_index){
                result = true;
                break;
            }
        }
    }
    return result;
}
#else //BLK_DEFER_LIST
bool blk_deferred_request_already_added( blk_deferred_request_t* dio_req, blk_descr_array_index_t block_index )
{
    int dios_index;
    for (dios_index = 0; dios_index < dio_req->dios_cnt; ++dios_index){
        blk_deferred_t* dio = dio_req->dios[dios_index];

        if (dio->blk_index == block_index){
            return true;
        }
    }
    return false;
}
#endif//BLK_DEFER_LIST


#ifdef BLK_DEFER_LIST
int blk_deferred_request_add( blk_deferred_request_t* dio_req, blk_deferred_t* dio )
{
    list_add_tail( &dio->link, &dio_req->dios );
    dio_req->sect_len += dio->sect.cnt;

    return SUCCESS;
}
#else //BLK_DEFER_LIST
int blk_deferred_request_add( blk_deferred_request_t* dio_req, blk_deferred_t* dio )
{
    if (dio_req->dios_cnt < DEFER_IO_DIO_REQUEST_LENGTH){
        dio_req->dios[dio_req->dios_cnt] = dio;
        ++dio_req->dios_cnt;

        dio_req->sect_len += dio->sect.cnt;
        return SUCCESS;
    }
    return -ENODATA;
}
#endif //BLK_DEFER_LIST


#ifdef BLK_DEFER_LIST
void blk_deferred_request_free( blk_deferred_request_t* dio_req )
{
    if (dio_req != NULL){
        while (!list_empty( &dio_req->dios )){
            blk_deferred_t* dio = list_entry( dio_req->dios.next, blk_deferred_t, link );

            list_del( &dio->link );

            blk_deferred_free( dio );
        }
        dbg_kfree( dio_req );
    }
}
#else //BLK_DEFER_LIST
void blk_deferred_request_free( blk_deferred_request_t* dio_req )
{
    if (dio_req != NULL){
        int inx = 0;

        for (inx = 0; inx < dio_req->dios_cnt; ++inx){
            if (dio_req->dios[inx]){
                blk_deferred_free( dio_req->dios[inx] );
                dio_req->dios[inx] = NULL;
            }
        }
        dbg_kfree( dio_req );
    }
}
#endif //BLK_DEFER_LIST

void blk_deferred_request_waiting_skip( blk_deferred_request_t* dio_req )
{
    init_completion( &dio_req->complete );
    atomic64_set( &dio_req->sect_processed, 0 );
}

int blk_deferred_request_wait( blk_deferred_request_t* dio_req )
{
    u64 start_jiffies = get_jiffies_64( );
    u64 current_jiffies;
    //wait_for_completion_io_timeout

    //if (0 == wait_for_completion_timeout( &dio_req->complete, (HZ * 30) )){
    while (0 == wait_for_completion_timeout( &dio_req->complete, (HZ * 1) )){
        //log_warnln( "Defer IO request timeout" );
        //log_err_sect( "sect_len=", dio_req->sect_len );
        //log_err_sect( "sect_processed=", atomic64_read( &dio_req->sect_processed ) );
        //return -EFAULT;

        current_jiffies = get_jiffies_64( );
        if (jiffies_to_msecs( current_jiffies - start_jiffies ) > 60 * 1000){
            log_warn( "Defer IO request timeout" );
            //log_err_sect( "sect_processed=", atomic64_read( &dio_req->sect_processed ) );
            //log_err_sect( "sect_len=", dio_req->sect_len );
            return -EDEADLK;
        }
    }

    return dio_req->result;
}

int blk_deferred_request_read_original( struct block_device* original_blk_dev, blk_deferred_request_t* dio_copy_req )
{
    int res = -ENODATA;
#ifndef BLK_DEFER_LIST
    int dio_inx = 0;
#endif

    blk_deferred_request_waiting_skip( dio_copy_req );

#ifdef BLK_DEFER_LIST
    if (!list_empty( &dio_copy_req->dios )){
        struct list_head* _list_head;
        list_for_each( _list_head, &dio_copy_req->dios ){
            blk_deferred_t* dio = list_entry( _list_head, blk_deferred_t, link );
#else
    for (dio_inx = 0; dio_inx < dio_copy_req->dios_cnt; ++dio_inx){
        blk_deferred_t* dio = dio_copy_req->dios[dio_inx];
        {
#endif

            sector_t page_array_ofs = 0;
            sector_t ofs = dio->sect.ofs;
            sector_t cnt = dio->sect.cnt;

            if (cnt != blk_deferred_submit_pages( original_blk_dev, dio_copy_req, READ, page_array_ofs, dio->buff, ofs, cnt )){
                log_err_sect( "Failed to submit reading defer IO request. ofs=", dio->sect.ofs );
                res = -EIO;
                break;
            }
            else
                res = SUCCESS;

            page_array_ofs += cnt;
        }
    }

    if (res == SUCCESS)
        res = blk_deferred_request_wait( dio_copy_req );

    return res;
}

int blk_deferred_request_store_file( struct block_device* blk_dev, blk_deferred_request_t* dio_copy_req )
{
    int res = SUCCESS;
#ifndef BLK_DEFER_LIST
    int dio_inx = 0;
#endif

    blk_deferred_request_waiting_skip( dio_copy_req );

#ifdef BLK_DEFER_LIST
    if (!list_empty( &dio_copy_req->dios )){
        struct list_head* _list_head;
        list_for_each( _list_head, &dio_copy_req->dios ){
            blk_deferred_t* dio = list_entry( _list_head, blk_deferred_t, link );
#else
    for (dio_inx = 0; dio_inx < dio_copy_req->dios_cnt; ++dio_inx)
    {
        blk_deferred_t* dio = dio_copy_req->dios[dio_inx];
        {
#endif

            range_t* rg;
            sector_t page_array_ofs = 0;
            blk_descr_file_t* blk_descr = (blk_descr_file_t*)dio->blk_descr;

            BUG_ON( NULL == dio );
            BUG_ON( NULL == dio->blk_descr );

            RANGELIST_FOREACH_BEGIN( blk_descr->rangelist, rg )
            {
                sector_t process_sect;
                BUG_ON( NULL == dio->buff );

                //log_err_range( "rg=", (*rg) );

                process_sect = blk_deferred_submit_pages( blk_dev, dio_copy_req, WRITE, page_array_ofs, dio->buff, rg->ofs, rg->cnt );
                BUG_ON( rg->cnt != process_sect );

                if (rg->cnt != process_sect){
                    log_err_sect( "Failed to submit defer IO request for storing. ofs=", dio->sect.ofs );
                    res = -EIO;
                    break;
                }
                page_array_ofs += rg->cnt;
            }
            RANGELIST_FOREACH_END( );

            if (res != SUCCESS)
                break;
        }
    }

    if (res != SUCCESS)
        return res;
    
    res = blk_deferred_request_wait( dio_copy_req );
    return res;
}

#ifdef SNAPSTORE_MULTIDEV
int blk_deferred_request_store_multidev( blk_deferred_request_t* dio_copy_req )
{
    int res = SUCCESS;
#ifndef BLK_DEFER_LIST
    int dio_inx = 0;
#endif

    blk_deferred_request_waiting_skip( dio_copy_req );

#ifdef BLK_DEFER_LIST
    if (!list_empty( &dio_copy_req->dios )){
        struct list_head* _list_head;
        list_for_each( _list_head, &dio_copy_req->dios ){
            blk_deferred_t* dio = list_entry( _list_head, blk_deferred_t, link );
#else
    for (dio_inx = 0; dio_inx < dio_copy_req->dios_cnt; ++dio_inx)
    {
        blk_deferred_t* dio = dio_copy_req->dios[dio_inx];
        {
#endif
            range_t* rg;
            void** p_extension;
            sector_t page_array_ofs = 0;
            blk_descr_multidev_t* blk_descr = (blk_descr_multidev_t*)dio->blk_descr;

            BUG_ON( NULL == dio );
            BUG_ON( NULL == dio->blk_descr );

            RANGELIST_EX_FOREACH_BEGIN( blk_descr->rangelist, rg, p_extension )
            {
                sector_t process_sect;
                struct block_device* blk_dev = (struct block_device*)(*p_extension);

                BUG_ON( NULL == dio->buff );

                //log_err_range( "rg=", (*rg) );

                process_sect = blk_deferred_submit_pages( blk_dev, dio_copy_req, WRITE, page_array_ofs, dio->buff, rg->ofs, rg->cnt );
                BUG_ON( rg->cnt != process_sect );

                if (rg->cnt != process_sect){
                    log_err_sect( "Failed to submit defer IO request for storing. ofs=", dio->sect.ofs );
                    res = -EIO;
                    break;
                }
                page_array_ofs += rg->cnt;
            }
            RANGELIST_EX_FOREACH_END( );

            if (res != SUCCESS)
                break;
        }
    }

    if (res != SUCCESS)
        return res;

    res = blk_deferred_request_wait( dio_copy_req );
    return res;
}
#endif

int blk_deffered_request_store_mem( blk_deferred_request_t* dio_copy_req )
{
    int res = SUCCESS;
#ifndef BLK_DEFER_LIST
    int dio_inx = 0;
#endif
    sector_t processed = 0;

#ifdef BLK_DEFER_LIST
    if (!list_empty( &dio_copy_req->dios )){
        struct list_head* _list_head;
        list_for_each( _list_head, &dio_copy_req->dios ){
            blk_deferred_t* dio = list_entry( _list_head, blk_deferred_t, link );
#else
    for (dio_inx = 0; dio_inx < dio_copy_req->dios_cnt; ++dio_inx)
    {
        blk_deferred_t* dio = dio_copy_req->dios[dio_inx];
        {
#endif
            blk_descr_mem_t* blk_descr = (blk_descr_mem_t*)dio->blk_descr;

            size_t portion = page_array_pages2mem( blk_descr->buff, 0, dio->buff, (SNAPSTORE_BLK_SIZE * SECTOR_SIZE) );
            if (unlikely( portion != (SNAPSTORE_BLK_SIZE * SECTOR_SIZE) )){
                res = -EIO;
                break;
            }
            processed += sector_from_size( portion );
        }
    }

    blk_deferred_complete( dio_copy_req, processed, res );
    return res;
}
