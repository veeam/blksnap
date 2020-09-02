#include "stdafx.h"
#include "defer_io.h"
#include "queue_spinlocking.h"
#include "blk_deferred.h"
#include "tracker.h"
#include "blk_util.h"

#ifdef CONFIG_BLK_FILTER
blk_qc_t filter_submit_original_bio(struct bio *bio);
#endif

#define SECTION "defer_io  "
#include "log_format.h"


typedef struct defer_io_original_request_s{
    queue_content_sl_t content;

    struct bio* bio;
#ifdef CONFIG_BLK_FILTER
    //use block io filters api
#else
    range_t sect;
    struct request_queue *q;
    make_request_fn* make_rq_fn;
#endif
    tracker_t* tracker;

}defer_io_original_request_t;


void _defer_io_finish( defer_io_t* defer_io, queue_sl_t* queue_in_progress )
{
    while ( !queue_sl_empty( *queue_in_progress ) )
    {
        tracker_t* tracker = NULL;
        bool cbt_locked = false;
        bool is_write_bio;
#ifdef CONFIG_BLK_FILTER
        sector_t sectCount = 0;
#endif
        defer_io_original_request_t* orig_req = (defer_io_original_request_t*)queue_sl_get_first( queue_in_progress );

        is_write_bio = bio_data_dir( orig_req->bio ) && bio_has_data( orig_req->bio );


        if (orig_req->tracker && is_write_bio){
            tracker = orig_req->tracker;
            cbt_locked = tracker_cbt_bitmap_lock( tracker );
            if (cbt_locked)
            {
#ifdef CONFIG_BLK_FILTER
                sectCount = sector_from_size(bio_bi_size(orig_req->bio));
                tracker_cbt_bitmap_set(tracker, bio_bi_sector(orig_req->bio), sectCount);
#else
                tracker_cbt_bitmap_set(tracker, orig_req->sect.ofs, orig_req->sect.cnt);
#endif

            }
        }

        {
            struct bio* _bio = orig_req->bio;
            orig_req->bio = NULL;

            bio_put(_bio); //bio_put should be before orig_req->make_rq_fn
#ifdef CONFIG_BLK_FILTER
            filter_submit_original_bio(_bio);
#else
            orig_req->make_rq_fn( orig_req->q, _bio );
#endif
        }
        atomic64_inc(&defer_io->state_bios_processed);
#ifdef CONFIG_BLK_FILTER
        atomic64_add(sectCount, &defer_io->state_sectors_processed);
#else
        atomic64_add((orig_req->sect.cnt), &defer_io->state_sectors_processed);
#endif

        if (cbt_locked)
            tracker_cbt_bitmap_unlock( tracker );

        queue_content_sl_free( &orig_req->content );
    }
}

int _defer_io_copy_prepare( defer_io_t* defer_io, queue_sl_t* queue_in_process, blk_deferred_request_t** dio_copy_req )
{
    int res = SUCCESS;
    int dios_count = 0;
    sector_t dios_sectors_count = 0;

    //fill copy_request set
    while (!queue_sl_empty( defer_io->dio_queue ) && (dios_count < DEFER_IO_DIO_REQUEST_LENGTH) && (dios_sectors_count < DEFER_IO_DIO_REQUEST_SECTORS_COUNT)){

        defer_io_original_request_t* dio_orig_req = (defer_io_original_request_t*)queue_sl_get_first( &defer_io->dio_queue );
        atomic_dec( &defer_io->queue_filling_count );

        queue_sl_push_back( queue_in_process, &dio_orig_req->content );

        if (!kthread_should_stop( ) && !snapstore_device_is_corrupted( defer_io->snapstore_device )) {
            if (bio_data_dir( dio_orig_req->bio ) && bio_has_data( dio_orig_req->bio )) {
#ifdef CONFIG_BLK_FILTER
                range_t copy_range;
                copy_range.ofs = bio_bi_sector(dio_orig_req->bio);
                copy_range.cnt = sector_from_size(bio_bi_size(dio_orig_req->bio));
                res = snapstore_device_prepare_requests(defer_io->snapstore_device, &copy_range, dio_copy_req);
#else
                res = snapstore_device_prepare_requests( defer_io->snapstore_device, &dio_orig_req->sect, dio_copy_req );
#endif
                if (res != SUCCESS){
                    log_err_d( "Unable to execute Copy On Write algorithm: failed to add ranges to copy to snapstore request. errno=", res );
                    break;
                }
#ifdef CONFIG_BLK_FILTER
                dios_sectors_count += copy_range.cnt;
#else
                dios_sectors_count += dio_orig_req->sect.cnt;
#endif
            }
        }
        ++dios_count;
    }
    return res;
}

int defer_io_work_thread( void* p )
{

    queue_sl_t queue_in_process;
    defer_io_t* defer_io = NULL;

    //set_user_nice( current, -20 ); //MIN_NICE

    if (SUCCESS != queue_sl_init( &queue_in_process, sizeof( defer_io_original_request_t ) )){
        log_err( "Failed to initialize queue for defer IO requests" );
        return -EFAULT;
    }

    defer_io = defer_io_get_resource((defer_io_t*)p);
    log_tr_format("Defer IO thread for original device [%d:%d] started", MAJOR(defer_io->original_dev_id), MINOR(defer_io->original_dev_id));

    while (!kthread_should_stop( ) || !queue_sl_empty( defer_io->dio_queue )){

        if (queue_sl_empty( defer_io->dio_queue )){
            int res = wait_event_interruptible_timeout( defer_io->queue_add_event, (!queue_sl_empty( defer_io->dio_queue )), VEEAMIMAGE_THROTTLE_TIMEOUT );
            if (-ERESTARTSYS == res){
                log_err( "Signal received in defer IO thread. Waiting for completion with code ERESTARTSYS" );
            }
            else{
                //if (res == 0) // timeout
                //    wake_up_interruptible( &defer_io->queue_throttle_waiter );
            }
        }

        if (!queue_sl_empty( defer_io->dio_queue )){
            int dio_copy_result = SUCCESS;
            blk_deferred_request_t* dio_copy_req = NULL;


            _snapstore_device_descr_read_lock( defer_io->snapstore_device );
            do{
                dio_copy_result = _defer_io_copy_prepare( defer_io, &queue_in_process, &dio_copy_req );
                if (dio_copy_result != SUCCESS){
                    log_err_d( "Unable to process defer IO request: failed to prepare copy request", dio_copy_result );
                    break;
                }
                if (NULL == dio_copy_req)
                    break;//nothing to copy

                dio_copy_result = blk_deferred_request_read_original( defer_io->original_blk_dev, dio_copy_req );
                if (dio_copy_result != SUCCESS){
                    log_err_d( "Unable to process defer IO request: failed to read data to copy request. errno=", dio_copy_result );
                    break;
                }
                dio_copy_result = snapstore_device_store( defer_io->snapstore_device, dio_copy_req );
                if (dio_copy_result != SUCCESS){
                    log_err_d( "Unable to process defer IO request: failed to write data from copy request. errno=", dio_copy_result );
                    break;
                }

                atomic64_add( dio_copy_req->sect_len, &defer_io->state_sectors_copy_read );
            } while (false);

            _defer_io_finish( defer_io, &queue_in_process );

            _snapstore_device_descr_read_unlock( defer_io->snapstore_device );

            if (dio_copy_req){
                if (dio_copy_result == -EDEADLK)
                    blk_deferred_request_deadlocked( dio_copy_req );
                else
                    blk_deferred_request_free( dio_copy_req );
            }
        }

        //wake up snapimage if defer io queue empty
        if (queue_sl_empty( defer_io->dio_queue )){
            wake_up_interruptible( &defer_io->queue_throttle_waiter );
        }
    }
    queue_sl_active( &defer_io->dio_queue, false );

    //waiting for all sent request complete
    _defer_io_finish( defer_io, &defer_io->dio_queue );

    if (SUCCESS != queue_sl_done( &queue_in_process)){
        log_err( "Failed to free up queue for defer IO requests" );
    }

    log_tr_format( "Defer IO thread for original device [%d:%d] completed", MAJOR( defer_io->original_dev_id ), MINOR( defer_io->original_dev_id ) );
    defer_io_put_resource(defer_io);
    return SUCCESS;
}

void _defer_io_destroy( void* this_resource )
{
    defer_io_t* defer_io = (defer_io_t*)this_resource;

    if (NULL == defer_io)
        return;
    {
        stream_size_t processed;
        stream_size_t copyed;

        processed = atomic64_read( &defer_io->state_sectors_processed );
        copyed = atomic64_read( &defer_io->state_sectors_copy_read );

        log_tr_format( "%lld MiB was processed", (processed >> (20-SECTOR_SHIFT)) );
        log_tr_format( "%lld MiB was copied", (copyed >> (20 - SECTOR_SHIFT)) );
    }
    if (defer_io->dio_thread)
        defer_io_stop( defer_io );

    queue_sl_done( &defer_io->dio_queue );

    if (defer_io->snapstore_device)
        snapstore_device_put_resource(defer_io->snapstore_device);
    
    dbg_kfree(defer_io);
    log_tr("Defer IO processor was destroyed");
}


int defer_io_create( dev_t dev_id, struct block_device* blk_dev, defer_io_t** pp_defer_io )
{
    int res = SUCCESS;
    defer_io_t* defer_io = NULL;
    //char thread_name[32];

    log_tr_dev_t( "Defer IO processor was created for device ", dev_id );

    defer_io = dbg_kzalloc( sizeof( defer_io_t ), GFP_KERNEL );
    if (defer_io == NULL)
        return -ENOMEM;

    do{
        atomic64_set( &defer_io->state_bios_received, 0 );
        atomic64_set( &defer_io->state_bios_processed, 0 );
        atomic64_set( &defer_io->state_sectors_received, 0 );
        atomic64_set( &defer_io->state_sectors_processed, 0 );
        atomic64_set( &defer_io->state_sectors_copy_read, 0 );

        defer_io->original_dev_id = dev_id;
        defer_io->original_blk_dev = blk_dev;

        {
            snapstore_device_t* snapstore_device = snapstore_device_find_by_dev_id( defer_io->original_dev_id );
            if (NULL == snapstore_device){
                log_err_dev_t( "Unable to create defer IO processor: failed to initialize snapshot data for device ", dev_id );
                res = -ENODATA;
                break;
            }
            defer_io->snapstore_device = snapstore_device_get_resource( snapstore_device );
        }


        res = queue_sl_init( &defer_io->dio_queue, sizeof( defer_io_original_request_t ) );

        init_waitqueue_head( &defer_io->queue_add_event );

        atomic_set( &defer_io->queue_filling_count, 0 );

        init_waitqueue_head( &defer_io->queue_throttle_waiter );

        shared_resource_init( &defer_io->sharing_header, defer_io, _defer_io_destroy );

        //if (sprintf( thread_name, "%s%d:%d", "veeamdeferio", MAJOR( dev_id ), MINOR( dev_id ) ) >= DISK_NAME_LEN){
        //    log_err_dev_t( "Failed to create defer IO processor. Cannot create thread name for device ", dev_id );
        //    res = -EINVAL;
        //    break;
        //}

        defer_io->dio_thread = kthread_create( defer_io_work_thread, (void *)defer_io, "veeamdeferio%d:%d", MAJOR( dev_id ), MINOR( dev_id ) );
        if (IS_ERR( defer_io->dio_thread )) {
            res = PTR_ERR( defer_io->dio_thread );
            log_err_d( "Unable to create defer IO processor: failed to create thread. errno=", res );
            break;
        }
        wake_up_process( defer_io->dio_thread );

    } while (false);

    if (res == SUCCESS){

        *pp_defer_io = defer_io;
        log_tr( "Defer IO processor was created" );
    }
    else{
        _defer_io_destroy( defer_io );
        defer_io = NULL;
        log_err_d( "Failed to create defer IO processor. errno=", res );
    }

    return res;
}


int defer_io_stop( defer_io_t* defer_io )
{
    int res = SUCCESS;

    log_tr_dev_t( "Defer IO thread for the device stopped ", defer_io->original_dev_id );
    if (defer_io->dio_thread != NULL){
        struct task_struct* dio_thread = defer_io->dio_thread;
        defer_io->dio_thread = NULL;

        res = kthread_stop( dio_thread );//stopping and waiting.
        if (res != SUCCESS){
            log_err_d( "Failed to stop defer IO thread. errno=", res );
        }
    }
    return res;
}

#ifdef CONFIG_BLK_FILTER
int defer_io_redirect_bio(defer_io_t* defer_io, struct bio *bio, void* tracker)
#else
int defer_io_redirect_bio( defer_io_t* defer_io, struct bio *bio, sector_t sectStart, sector_t sectCount, struct request_queue *q, make_request_fn* TargetMakeRequest_fn, void* tracker )
#endif
{
#ifdef CONFIG_BLK_FILTER
    sector_t sectCount;
#endif

    defer_io_original_request_t* dio_orig_req;

    if (snapstore_device_is_corrupted( defer_io->snapstore_device ))
        return -ENODATA;

    dio_orig_req = (defer_io_original_request_t*)queue_content_sl_new_opt( &defer_io->dio_queue, GFP_NOIO );
    if (dio_orig_req == NULL)
        return -ENOMEM;

    
#ifdef CONFIG_BLK_FILTER
    sectCount = sector_from_size(bio_bi_size(bio));
#else
    //copy data from bio to dio write buffer
    dio_orig_req->q = q;
    dio_orig_req->make_rq_fn = TargetMakeRequest_fn;

    dio_orig_req->sect.ofs = sectStart;
    dio_orig_req->sect.cnt = sectCount;
#endif

    bio_get(dio_orig_req->bio = bio);

    dio_orig_req->tracker = (tracker_t*)tracker;

    if (SUCCESS != queue_sl_push_back( &defer_io->dio_queue, &dio_orig_req->content )){
        queue_content_sl_free( &dio_orig_req->content );
        return -EFAULT;
    }

    atomic64_inc( &defer_io->state_bios_received );
    atomic64_add( sectCount, &defer_io->state_sectors_received );

    atomic_inc( &defer_io->queue_filling_count );

    wake_up_interruptible( &defer_io->queue_add_event );

    return SUCCESS;
}


void defer_io_print_state( defer_io_t* defer_io )
{
    unsigned long received_mb;
    unsigned long processed_mb;
    unsigned long copy_read_mb;

    log_tr( "" );
    log_tr( "Defer IO state:" );

    log_tr_d( "requests in queue count=",
        atomic_read( &defer_io->queue_filling_count ) );

    log_tr_format( "bios: received=%lld processed=%lld",
        (long long int)atomic64_read( &defer_io->state_bios_received ),
        (long long int)atomic64_read( &defer_io->state_bios_processed ) );

    log_tr_format( "sectors: received=%lld processed=%lld copy_read=%lld",
        (long long int)atomic64_read( &defer_io->state_sectors_received ),
        (long long int)atomic64_read( &defer_io->state_sectors_processed ),
        (long long int)atomic64_read( &defer_io->state_sectors_copy_read ) );

    received_mb = (unsigned long)(atomic64_read( &defer_io->state_sectors_received ) >> (20 - SECTOR_SHIFT));
    processed_mb = (unsigned long)(atomic64_read( &defer_io->state_sectors_processed ) >> (20 - SECTOR_SHIFT));
    copy_read_mb = (unsigned long)(atomic64_read( &defer_io->state_sectors_copy_read ) >> (20 - SECTOR_SHIFT));

    log_tr_format( "bytes: received=%ld MiB processed=%ld MiB copy_read=%ld MiB",
        received_mb,
        processed_mb,
        copy_read_mb);

    if (defer_io->snapstore_device)
        snapstore_device_print_state( defer_io->snapstore_device );
}

