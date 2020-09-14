#include "common.h"
#include "queue_spinlocking.h"

#define SECTION "queue     "
#include "log_format.h"

void queue_sl_init( queue_sl_t* queue, int content_size )
{
	INIT_LIST_HEAD( &queue->headList );

	spin_lock_init( &queue->lock );

	queue->content_size = content_size;

	atomic_set( &queue->in_queue_cnt, 0);
	atomic_set( &queue->alloc_cnt, 0 );

	atomic_set( &queue->active_state, true );
}

void queue_sl_done( queue_sl_t* queue )
{
	if (queue->content_size != 0){
		atomic_set( &queue->active_state, false );

		if (!list_empty( &queue->headList ))
			log_err_format( "CRITICAL ERROR: Queue is not empty. %d items left", atomic_read( &queue->in_queue_cnt ) );

		if (atomic_read( &queue->alloc_cnt ))
			log_err_format( "CRITICAL ERROR: Some content is not free. %d allocated items left", atomic_read( &queue->alloc_cnt ) );

		queue->content_size = 0;
	}
}

queue_content_sl_t* queue_content_sl_new_opt_append( queue_sl_t* queue, gfp_t gfp_opt, size_t append_size )
{
	queue_content_sl_t* content = kzalloc( queue->content_size + append_size, gfp_opt );

	if (NULL == content)
		return NULL;

	atomic_inc( &queue->alloc_cnt );

	INIT_LIST_HEAD( &content->link );
	content->queue = queue;

	return content;
}

void queue_content_sl_free( queue_content_sl_t* content )
{
	if (content) {
		atomic_dec( &content->queue->alloc_cnt );

		kfree( content );
	}
}

int queue_sl_push_back( queue_sl_t* queue, queue_content_sl_t* content )
{
	int res = SUCCESS;

	spin_lock( &queue->lock );

	if (atomic_read( &queue->active_state )) {
		INIT_LIST_HEAD( &content->link );
		list_add_tail( &content->link, &(queue)->headList );
		atomic_inc( &queue->in_queue_cnt );
	}
	else
		res = -EACCES;

	spin_unlock( &queue->lock );
	return res;
}

queue_content_sl_t* queue_sl_get_first( queue_sl_t* queue )
{
	queue_content_sl_t* content = NULL;

	spin_lock( &queue->lock );

	if (!list_empty( &queue->headList )) {
		content = list_entry( queue->headList.next, queue_content_sl_t, link );
		list_del( &content->link );
		atomic_dec( &queue->in_queue_cnt );
	}

	spin_unlock( &queue->lock );

	return content;
}

bool queue_sl_active( queue_sl_t* queue, bool state )
{
	bool prev_state;

	spin_lock( &queue->lock );

	prev_state = atomic_read( &queue->active_state );
	atomic_set( &queue->active_state, state );

	spin_unlock( &queue->lock );

	return prev_state;
}
