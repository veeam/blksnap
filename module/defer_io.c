// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-defer_io"
#include "common.h"
#include "defer_io.h"
#include "blk_deferred.h"
#include "tracker.h"
#include "blk_util.h"
#include "filter.h"

#include <linux/kthread.h>

#define BLK_IMAGE_THROTTLE_TIMEOUT (1 * HZ) //delay 1 sec
//#define BLK_IMAGE_THROTTLE_TIMEOUT ( HZ/1000 * 10 )	//delay 10 ms



struct defer_io_orig_rq {
	struct list_head link;
	struct defer_io_queue *queue;

	struct bio *bio;
	struct tracker *tracker;
};

void defer_io_queue_init(struct defer_io_queue *queue)
{
	INIT_LIST_HEAD(&queue->list);

	spin_lock_init(&queue->lock);

	atomic_set(&queue->in_queue_cnt, 0);
	atomic_set(&queue->active_state, true);
}

struct defer_io_orig_rq *defer_io_queue_new(struct defer_io_queue *queue)
{
	struct defer_io_orig_rq *dio_rq = kzalloc(sizeof(struct defer_io_orig_rq), GFP_NOIO);

	if (dio_rq == NULL)
		return NULL;

	INIT_LIST_HEAD(&dio_rq->link);
	dio_rq->queue = queue;

	return dio_rq;
}

void defer_io_queue_free(struct defer_io_orig_rq *dio_rq)
{
	if (dio_rq)
		kfree(dio_rq);
}

int defer_io_queue_push_back(struct defer_io_queue *queue, struct defer_io_orig_rq *dio_rq)
{
	int res = SUCCESS;

	spin_lock(&queue->lock);

	if (atomic_read(&queue->active_state)) {
		INIT_LIST_HEAD(&dio_rq->link);

		list_add_tail(&dio_rq->link, &queue->list);
		atomic_inc(&queue->in_queue_cnt);
	} else
		res = -EACCES;

	spin_unlock(&queue->lock);
	return res;
}

struct defer_io_orig_rq *defer_io_queue_get_first(struct defer_io_queue *queue)
{
	struct defer_io_orig_rq *dio_rq = NULL;

	spin_lock(&queue->lock);

	if (!list_empty(&queue->list)) {
		dio_rq = list_entry(queue->list.next, struct defer_io_orig_rq, link);
		list_del(&dio_rq->link);
		atomic_dec(&queue->in_queue_cnt);
	}

	spin_unlock(&queue->lock);

	return dio_rq;
}

bool defer_io_queue_active(struct defer_io_queue *queue, bool state)
{
	bool prev_state;

	spin_lock(&queue->lock);

	prev_state = atomic_read(&queue->active_state);
	atomic_set(&queue->active_state, state);

	spin_unlock(&queue->lock);

	return prev_state;
}

#define defer_io_queue_empty(queue) (atomic_read(&(queue).in_queue_cnt) == 0)

void _defer_io_finish(struct defer_io *defer_io, struct defer_io_queue *queue_in_progress)
{
	while (!defer_io_queue_empty(*queue_in_progress)) {
		struct tracker *tracker = NULL;
		bool cbt_locked = false;
		bool is_write_bio;
		sector_t sectCount = 0;

		struct defer_io_orig_rq *orig_req = defer_io_queue_get_first(queue_in_progress);

		is_write_bio = bio_data_dir(orig_req->bio) && bio_has_data(orig_req->bio);

		if (orig_req->tracker && is_write_bio) {
			tracker = orig_req->tracker;
			cbt_locked = tracker_cbt_bitmap_lock(tracker);
			if (cbt_locked) {
				sectCount = bio_sectors(orig_req->bio);
				tracker_cbt_bitmap_set(tracker, orig_req->bio->bi_iter.bi_sector,
						       sectCount);
			}
		}

		bio_put(orig_req->bio);
		filter_submit_original_bio(orig_req->bio);

		if (cbt_locked)
			tracker_cbt_bitmap_unlock(tracker);

		defer_io_queue_free(orig_req);
	}
}

int _defer_io_copy_prepare(struct defer_io *defer_io, struct defer_io_queue *queue_in_process,
			   struct blk_deferred_request **dio_copy_req)
{
	int res = SUCCESS;
	int dios_count = 0;
	sector_t dios_sectors_count = 0;

	//fill copy_request set
	while (!defer_io_queue_empty(defer_io->dio_queue) &&
	       (dios_count < DEFER_IO_DIO_REQUEST_LENGTH) &&
	       (dios_sectors_count < DEFER_IO_DIO_REQUEST_SECTORS_COUNT)) {
		struct defer_io_orig_rq *dio_orig_req =
			(struct defer_io_orig_rq *)defer_io_queue_get_first(&defer_io->dio_queue);
		atomic_dec(&defer_io->queue_filling_count);

		defer_io_queue_push_back(queue_in_process, dio_orig_req);

		if (!kthread_should_stop() &&
		    !snapstore_device_is_corrupted(defer_io->snapstore_device)) {
			if (bio_data_dir(dio_orig_req->bio) && bio_has_data(dio_orig_req->bio)) {
				struct blk_range copy_range;

				copy_range.ofs = dio_orig_req->bio->bi_iter.bi_sector;
				copy_range.cnt = bio_sectors(dio_orig_req->bio);
				res = snapstore_device_prepare_requests(defer_io->snapstore_device,
									&copy_range, dio_copy_req);
				if (res != SUCCESS) {
					pr_err("Unable to execute Copy On Write algorithm: failed to add ranges to copy to snapstore request. errno=%d\n",
					       res);
					break;
				}

				dios_sectors_count += copy_range.cnt;
			}
		}
		++dios_count;
	}
	return res;
}

int defer_io_work_thread(void *p)
{
	struct defer_io_queue queue_in_process = { 0 };
	struct defer_io *defer_io = NULL;

	//set_user_nice( current, -20 ); //MIN_NICE
	defer_io_queue_init(&queue_in_process);

	defer_io = defer_io_get_resource((struct defer_io *)p);
	pr_info("Defer IO thread for original device [%d:%d] started\n",
		MAJOR(defer_io->original_dev_id), MINOR(defer_io->original_dev_id));

	while (!kthread_should_stop() || !defer_io_queue_empty(defer_io->dio_queue)) {
		if (defer_io_queue_empty(defer_io->dio_queue)) {
			int res = wait_event_interruptible_timeout(
				defer_io->queue_add_event,
				(!defer_io_queue_empty(defer_io->dio_queue)),
				BLK_IMAGE_THROTTLE_TIMEOUT);
			if (-ERESTARTSYS == res)
				pr_err("Signal received in defer IO thread. Waiting for completion with code ERESTARTSYS\n");
		}

		if (!defer_io_queue_empty(defer_io->dio_queue)) {
			int dio_copy_result = SUCCESS;
			struct blk_deferred_request *dio_copy_req = NULL;

			mutex_lock(&defer_io->snapstore_device->store_block_map_locker);
			do {
				dio_copy_result = _defer_io_copy_prepare(
					defer_io, &queue_in_process, &dio_copy_req);
				if (dio_copy_result != SUCCESS) {
					pr_err("Unable to process defer IO request: failed to prepare copy request. erro=%d\n",
					       dio_copy_result);
					break;
				}
				if (dio_copy_req == NULL)
					break; //nothing to copy

				dio_copy_result = blk_deferred_request_read_original(
					defer_io->original_blk_dev, dio_copy_req);
				if (dio_copy_result != SUCCESS) {
					pr_err("Unable to process defer IO request: failed to read data to copy request. errno=%d\n",
					       dio_copy_result);
					break;
				}
				dio_copy_result = snapstore_device_store(defer_io->snapstore_device,
									 dio_copy_req);
				if (dio_copy_result != SUCCESS) {
					pr_err("Unable to process defer IO request: failed to write data from copy request. errno=%d\n",
					       dio_copy_result);
					break;
				}

			} while (false);
			_defer_io_finish(defer_io, &queue_in_process);
			mutex_unlock(&defer_io->snapstore_device->store_block_map_locker);

			if (dio_copy_req) {
				if (dio_copy_result == -EDEADLK)
					blk_deferred_request_deadlocked(dio_copy_req);
				else
					blk_deferred_request_free(dio_copy_req);
			}
		}

		//wake up snapimage if defer io queue empty
		if (defer_io_queue_empty(defer_io->dio_queue)) {
			wake_up_interruptible(&defer_io->queue_throttle_waiter);
		}
	}
	defer_io_queue_active(&defer_io->dio_queue, false);

	//waiting for all sent request complete
	_defer_io_finish(defer_io, &defer_io->dio_queue);

	pr_info("Defer IO thread for original device [%d:%d] completed\n",
		MAJOR(defer_io->original_dev_id), MINOR(defer_io->original_dev_id));
	defer_io_put_resource(defer_io);
	return SUCCESS;
}

static void _defer_io_destroy(struct defer_io *defer_io)
{
	if (defer_io == NULL)
		return;

	if (defer_io->dio_thread)
		defer_io_stop(defer_io);

	if (defer_io->snapstore_device)
		snapstore_device_put_resource(defer_io->snapstore_device);

	kfree(defer_io);
	pr_info("Defer IO processor was destroyed\n");
}

static void defer_io_destroy_cb(struct kref *kref)
{
	_defer_io_destroy(container_of(kref, struct defer_io, refcount));
}

struct defer_io *defer_io_get_resource(struct defer_io *defer_io)
{
	if (defer_io)
		kref_get(&defer_io->refcount);

	return defer_io;
}

void defer_io_put_resource(struct defer_io *defer_io)
{
	if (defer_io)
		kref_put(&defer_io->refcount, defer_io_destroy_cb);
}

int defer_io_create(dev_t dev_id, struct block_device *blk_dev, struct defer_io **pp_defer_io)
{
	int res = SUCCESS;
	struct defer_io *defer_io = NULL;
	struct snapstore_device *snapstore_device;

	pr_info("Defer IO processor was created for device [%d:%d]\n", MAJOR(dev_id),
		MINOR(dev_id));

	defer_io = kzalloc(sizeof(struct defer_io), GFP_KERNEL);
	if (defer_io == NULL)
		return -ENOMEM;

	snapstore_device = snapstore_device_find_by_dev_id(dev_id);
	if (snapstore_device == NULL) {
		pr_err("Unable to create defer IO processor: failed to initialize snapshot data for device [%d:%d]\n",
		       MAJOR(dev_id), MINOR(dev_id));

		kfree(defer_io);
		return -ENODATA;
	}

	defer_io->snapstore_device = snapstore_device_get_resource(snapstore_device);
	defer_io->original_dev_id = dev_id;
	defer_io->original_blk_dev = blk_dev;

	kref_init(&defer_io->refcount);

	defer_io_queue_init(&defer_io->dio_queue);

	init_waitqueue_head(&defer_io->queue_add_event);

	atomic_set(&defer_io->queue_filling_count, 0);

	init_waitqueue_head(&defer_io->queue_throttle_waiter);

	defer_io->dio_thread = kthread_create(defer_io_work_thread, (void *)defer_io,
					      "blksnapdeferio%d:%d", MAJOR(dev_id), MINOR(dev_id));
	if (IS_ERR(defer_io->dio_thread)) {
		res = PTR_ERR(defer_io->dio_thread);
		pr_err("Unable to create defer IO processor: failed to create thread. errno=%d\n",
		       res);

		_defer_io_destroy(defer_io);
		defer_io = NULL;
		*pp_defer_io = NULL;

		return res;
	}

	wake_up_process(defer_io->dio_thread);

	*pp_defer_io = defer_io;
	pr_info("Defer IO processor was created\n");

	return SUCCESS;
}

int defer_io_stop(struct defer_io *defer_io)
{
	int res = SUCCESS;

	pr_info("Defer IO thread for the device stopped [%d:%d]\n",
		MAJOR(defer_io->original_dev_id), MINOR(defer_io->original_dev_id));

	if (defer_io->dio_thread != NULL) {
		struct task_struct *dio_thread = defer_io->dio_thread;
		defer_io->dio_thread = NULL;

		res = kthread_stop(dio_thread); //stopping and waiting.
		if (res != SUCCESS)
			pr_err("Failed to stop defer IO thread. errno=%d\n", res);
	}

	return res;
}

int defer_io_redirect_bio(struct defer_io *defer_io, struct bio *bio, void *tracker)
{
	struct defer_io_orig_rq *dio_orig_req;

	if (snapstore_device_is_corrupted(defer_io->snapstore_device))
		return -ENODATA;

	dio_orig_req = defer_io_queue_new(&defer_io->dio_queue);
	if (dio_orig_req == NULL)
		return -ENOMEM;

	bio_get(dio_orig_req->bio = bio);

	dio_orig_req->tracker = (struct tracker *)tracker;

	if (SUCCESS != defer_io_queue_push_back(&defer_io->dio_queue, dio_orig_req)) {
		defer_io_queue_free(dio_orig_req);
		return -EFAULT;
	}

	atomic_inc(&defer_io->queue_filling_count);

	wake_up_interruptible(&defer_io->queue_add_event);

	return SUCCESS;
}
