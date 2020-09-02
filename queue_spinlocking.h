#pragma once

typedef struct queue_sl_s
{
    struct list_head headList;
    spinlock_t lock;

    atomic_t active_state;
    int content_size;
    atomic_t in_queue_cnt;
    atomic_t alloc_cnt;
}queue_sl_t;

typedef struct queue_content_sl_s
{
    struct list_head link;
    queue_sl_t* queue;
}queue_content_sl_t;


int queue_sl_init( queue_sl_t* queue, int content_size );
int queue_sl_done( queue_sl_t* queue );

queue_content_sl_t* queue_content_sl_new_opt_append( queue_sl_t* queue, gfp_t gfp_opt, size_t append_size );
queue_content_sl_t* queue_content_sl_new_opt( queue_sl_t* queue, gfp_t gfp_opt );
void queue_content_sl_free( queue_content_sl_t* content );

int  queue_sl_push_back( queue_sl_t* queue, queue_content_sl_t* content );
queue_content_sl_t* queue_sl_get_first( queue_sl_t* queue );

bool queue_sl_active( queue_sl_t* queue, bool state );

#define queue_sl_length( queue ) \
    atomic_read( &(queue).in_queue_cnt )

#define queue_sl_empty( queue ) \
    (atomic_read( &(queue).in_queue_cnt ) == 0)

#define queue_sl_unactive( queue ) \
    ( (atomic_read( &((queue).active_state) ) == false) && (atomic_read( &((queue).alloc_cnt) ) == 0) )
