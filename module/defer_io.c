#include "common.h"
#include "defer_io.h"
#include "blk_deferred.h"
#include "tracker.h"
#include "blk_util.h"

#include <linux/kthread.h>

#define SECTION "defer_io  "
#include "log_format.h"

#define VEEAMIMAGE_THROTTLE_TIMEOUT ( 1*HZ )	//delay 1 sec
//#define VEEAMIMAGE_THROTTLE_TIMEOUT ( HZ/1000 * 10 )	//delay 10 ms

blk_qc_t filter_submit_original_bio(struct bio *bio);

typedef struct defer_io_original_request_s{
	struct list_head link;
	defer_io_queue_t *queue;

	struct bio* bio;
	tracker_t* tracker;

}defer_io_original_request_t;

void defer_io_queue_init( defer_io_queue_t* queue )
{
	INIT_LIST_HEAD( &queue->list );

	spin_lock_init( &queue->lock );

	atomic_set( &queue->in_queue_cnt, 0);
	atomic_set( &queue->active_state, true );
}

defer_io_original_request_t* defer_io_queue_new( defer_io_queue_t* queue )
{
	defer_io_original_request_t* dio_rq = kzalloc( sizeof(defer_io_original_request_t), GFP_NOIO );

	if (NULL == dio_rq)
		return NULL;

	INIT_LIST_HEAD( &dio_rq->link );
	dio_rq->queue = queue;

	return dio_rq;
}

void defer_io_queue_free( defer_io_original_request_t* dio_rq )
{
	if (dio_rq)
		kfree( dio_rq );
}

int defer_io_queue_push_back( defer_io_queue_t* queue, defer_io_original_request_t* dio_rq )
{
	int res = SUCCESS;

	spin_lock( &queue->lock );

	if (atomic_read( &queue->active_state )) {
		INIT_LIST_HEAD( &dio_rq->link );

		list_add_tail( &dio_rq->link, &queue->list );
		atomic_inc( &queue->in_queue_cnt );
	}
	else
		res = -EACCES;

	spin_unlock( &queue->lock );
	return res;
}

defer_io_original_request_t* defer_io_queue_get_first( defer_io_queue_t* queue )
{
	defer_io_original_request_t* dio_rq = NULL;

	spin_lock( &queue->lock );

	if (!list_empty( &queue->list )) {
		dio_rq = list_entry( queue->list.next, defer_io_original_request_t, link );
		list_del( &dio_rq->link );
		atomic_dec( &queue->in_queue_cnt );
	}

	spin_unlock( &queue->lock );

	return dio_rq;
}

bool defer_io_queue_active( defer_io_queue_t* queue, bool state )
{
	bool prev_state;

	spin_lock( &queue->lock );

	prev_state = atomic_read( &queue->active_state );
	atomic_set( &queue->active_state, state );

	spin_unlock( &queue->lock );

	return prev_state;
}

#define defer_io_queue_empty( queue ) \
	(atomic_read( &(queue).in_queue_cnt ) == 0)




void _defer_io_finish( defer_io_t* defer_io, defer_io_queue_t* queue_in_progress )
{
	while ( !defer_io_queue_empty( *queue_in_progress ) )
	{
		tracker_t* tracker = NULL;
		bool cbt_locked = false;
		bool is_write_bio;
		sector_t sectCount = 0;

		defer_io_original_request_t* orig_req = defer_io_queue_get_first( queue_in_progress );

		is_write_bio = bio_data_dir( orig_req->bio ) && bio_has_data( orig_req->bio );


		if (orig_req->tracker && is_write_bio){
			tracker = orig_req->tracker;
			cbt_locked = tracker_cbt_bitmap_lock( tracker );
			if (cbt_locked)
			{
				sectCount = bio_sectors(orig_req->bio);
				tracker_cbt_bitmap_set(tracker, orig_req->bio->bi_iter.bi_sector, sectCount);
			}
		}


		bio_put(orig_req->bio);
		filter_submit_original_bio(orig_req->bio);

		if (cbt_locked)
			tracker_cbt_bitmap_unlock( tracker );

		defer_io_queue_free( orig_req );
	}
}

int _defer_io_copy_prepare( defer_io_t* defer_io, defer_io_queue_t* queue_in_process, blk_deferred_request_t** dio_copy_req )
{
	int res = SUCCESS;
	int dios_count = 0;
	sector_t dios_sectors_count = 0;

	//fill copy_request set
	while (!defer_io_queue_empty( defer_io->dio_queue ) && (dios_count < DEFER_IO_DIO_REQUEST_LENGTH) && (dios_sectors_count < DEFER_IO_DIO_REQUEST_SECTORS_COUNT)){

		defer_io_original_request_t* dio_orig_req = (defer_io_original_request_t*)defer_io_queue_get_first( &defer_io->dio_queue );
		atomic_dec( &defer_io->queue_filling_count );

		defer_io_queue_push_back( queue_in_process, dio_orig_req );

		if (!kthread_should_stop( ) && !snapstore_device_is_corrupted( defer_io->snapstore_device )) {
			if (bio_data_dir( dio_orig_req->bio ) && bio_has_data( dio_orig_req->bio )) {
				struct blk_range copy_range;

				copy_range.ofs = dio_orig_req->bio->bi_iter.bi_sector;
				copy_range.cnt = bio_sectors(dio_orig_req->bio);
				res = snapstore_device_prepare_requests(defer_io->snapstore_device, &copy_range, dio_copy_req);
				if (res != SUCCESS){
					log_err_d( "Unable to execute Copy On Write algorithm: failed to add ranges to copy to snapstore request. errno=", res );
					break;
				}

				dios_sectors_count += copy_range.cnt;
			}
		}
		++dios_count;
	}
	return res;
}

int defer_io_work_thread( void* p )
{
	defer_io_queue_t queue_in_process = {0};
	defer_io_t* defer_io = NULL;

	//set_user_nice( current, -20 ); //MIN_NICE
	defer_io_queue_init( &queue_in_process );

	defer_io = defer_io_get_resource((defer_io_t*)p);
	log_tr_format("Defer IO thread for original device [%d:%d] started", MAJOR(defer_io->original_dev_id), MINOR(defer_io->original_dev_id));

	while (!kthread_should_stop( ) || !defer_io_queue_empty( defer_io->dio_queue )){

		if (defer_io_queue_empty( defer_io->dio_queue )){
			int res = wait_event_interruptible_timeout( defer_io->queue_add_event, (!defer_io_queue_empty( defer_io->dio_queue )), VEEAMIMAGE_THROTTLE_TIMEOUT );
			if (-ERESTARTSYS == res)
				log_err( "Signal received in defer IO thread. Waiting for completion with code ERESTARTSYS" );
		}

		if (!defer_io_queue_empty( defer_io->dio_queue )){
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
		if (defer_io_queue_empty( defer_io->dio_queue )){
			wake_up_interruptible( &defer_io->queue_throttle_waiter );
		}
	}
	defer_io_queue_active( &defer_io->dio_queue, false );

	//waiting for all sent request complete
	_defer_io_finish( defer_io, &defer_io->dio_queue );

	log_tr_format( "Defer IO thread for original device [%d:%d] completed", MAJOR( defer_io->original_dev_id ), MINOR( defer_io->original_dev_id ) );
	defer_io_put_resource(defer_io);
	return SUCCESS;
}

void _defer_io_destroy( void* this_resource )
{
	defer_io_t* defer_io = (defer_io_t*)this_resource;

	if (NULL == defer_io)
		return;

	if (defer_io->dio_thread)
		defer_io_stop( defer_io );

	if (defer_io->snapstore_device)
		snapstore_device_put_resource(defer_io->snapstore_device);

	kfree(defer_io);
	log_tr("Defer IO processor was destroyed");
}


int defer_io_create( dev_t dev_id, struct block_device* blk_dev, defer_io_t** pp_defer_io )
{
	int res = SUCCESS;
	defer_io_t* defer_io = NULL;
	snapstore_device_t* snapstore_device;

	log_tr_dev_t( "Defer IO processor was created for device ", dev_id );

	defer_io = kzalloc( sizeof( defer_io_t ), GFP_KERNEL );
	if (defer_io == NULL)
		return -ENOMEM;

	snapstore_device = snapstore_device_find_by_dev_id( dev_id );
	if (NULL == snapstore_device){
		log_err_dev_t( "Unable to create defer IO processor: failed to initialize snapshot data for device ", dev_id );

		kfree(defer_io);
		return -ENODATA;
	}

	defer_io->snapstore_device = snapstore_device_get_resource( snapstore_device );
	defer_io->original_dev_id = dev_id;
	defer_io->original_blk_dev = blk_dev;

	shared_resource_init( &defer_io->sharing_header, defer_io, _defer_io_destroy );

	defer_io_queue_init(&defer_io->dio_queue);

	init_waitqueue_head( &defer_io->queue_add_event );

	atomic_set( &defer_io->queue_filling_count, 0 );

	init_waitqueue_head( &defer_io->queue_throttle_waiter );

	defer_io->dio_thread = kthread_create( defer_io_work_thread, (void *)defer_io, "veeamdeferio%d:%d", MAJOR( dev_id ), MINOR( dev_id ) );
	if (IS_ERR( defer_io->dio_thread )) {
		res = PTR_ERR( defer_io->dio_thread );
		log_err_d( "Unable to create defer IO processor: failed to create thread. errno=", res );

		_defer_io_destroy( defer_io );
		*pp_defer_io = NULL;

		return res;
	}

	wake_up_process( defer_io->dio_thread );

	*pp_defer_io = defer_io;
	log_tr( "Defer IO processor was created" );

	return SUCCESS;
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

int defer_io_redirect_bio(defer_io_t* defer_io, struct bio *bio, void* tracker)
{
	defer_io_original_request_t* dio_orig_req;

	if (snapstore_device_is_corrupted( defer_io->snapstore_device ))
		return -ENODATA;

	dio_orig_req = defer_io_queue_new( &defer_io->dio_queue);
	if (dio_orig_req == NULL)
		return -ENOMEM;

	bio_get(dio_orig_req->bio = bio);

	dio_orig_req->tracker = (tracker_t*)tracker;

	if (SUCCESS != defer_io_queue_push_back( &defer_io->dio_queue, dio_orig_req )){
		defer_io_queue_free( dio_orig_req );
		return -EFAULT;
	}

	atomic_inc( &defer_io->queue_filling_count );

	wake_up_interruptible( &defer_io->queue_add_event );

	return SUCCESS;

}

