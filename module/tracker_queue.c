#include "common.h"
#include "container_spinlocking.h"
#include "tracker_queue.h"

#define SECTION "tracker   "

container_sl_t tracker_queue_container;

int tracker_queue_init(void)
{
	container_sl_init(&tracker_queue_container, sizeof(tracker_queue_t));
	return SUCCESS;
}

int tracker_queue_done(void)
{
	int result = container_sl_done(&tracker_queue_container);
	if (SUCCESS != result)
		log_err("Failed to free up tracker queue container");
	return result;
}

/**
  * find or create new tracker queue
  */
int tracker_queue_ref(struct gendisk *disk, u8 partno, tracker_queue_t** ptracker_queue)
{
	int find_result = SUCCESS;
	tracker_queue_t* tr_q = NULL;


	find_result = tracker_queue_find(disk, partno, &tr_q);
	if (SUCCESS == find_result) {
		log_tr("Tracker queue already exists");

		*ptracker_queue = tr_q;
		atomic_inc(&tr_q->atomic_ref_count);

		return find_result;
	}

	if (-ENODATA != find_result) {
		log_err_d("Cannot to find tracker queue. errno=", find_result);
		return find_result;
	}

	log_tr("New tracker queue create");

	tr_q = (tracker_queue_t*)content_sl_new(&tracker_queue_container);
	if (NULL == tr_q)
		return -ENOMEM;

	atomic_set(&tr_q->atomic_ref_count, 0);
	tr_q->disk = disk;
	tr_q->partno = partno;

	*ptracker_queue = tr_q;
	atomic_inc(&tr_q->atomic_ref_count);

	container_sl_push_back(&tracker_queue_container, &tr_q->content);

	log_tr("New tracker queue was created");

	return SUCCESS;
}

void tracker_queue_unref(tracker_queue_t* tracker_queue)
{
	if (atomic_dec_and_test(&tracker_queue->atomic_ref_count)) {
		tracker_queue->disk = NULL;
		tracker_queue->partno = 0;

		container_sl_free(&tracker_queue->content);

		log_tr("Tracker queue freed");
	}
	else
		log_tr("Tracker queue is in use");
}

int tracker_queue_find(struct gendisk *disk, u8 partno, tracker_queue_t** ptracker_queue)
{
	int result = -ENODATA;
	content_sl_t* pContent = NULL;
	tracker_queue_t* tr_q = NULL;
	CONTAINER_SL_FOREACH_BEGIN(tracker_queue_container, pContent)
	{
		tr_q = (tracker_queue_t*)pContent;
		if ((tr_q->disk == disk) && (tr_q->partno == partno)) {
			*ptracker_queue = tr_q;

			result = SUCCESS;	//don`t continue
			break;
		}
	}CONTAINER_SL_FOREACH_END(tracker_queue_container);

	return result;
}

