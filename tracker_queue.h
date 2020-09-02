#pragma once
#include "container_spinlocking.h"

#ifdef CONFIG_BLK_FILTER

typedef struct _tracker_queue_s
{
    content_sl_t content;
    struct gendisk *disk;
    u8 partno;
    atomic_t atomic_ref_count;

}tracker_queue_t;

int tracker_queue_init(void);
int tracker_queue_done(void);

int tracker_queue_ref(struct gendisk *disk, u8 partno, tracker_queue_t** ptracker_queue);
void tracker_queue_unref(tracker_queue_t* ptracker_queue);
int tracker_queue_find(struct gendisk *disk, u8 partno, tracker_queue_t** ptracker_queue);

#else //CONFIG_BLK_FILTER

typedef struct _tracker_queue_s
{
    content_sl_t content;
    struct request_queue*    original_queue;
    make_request_fn*        original_make_request_fn;

    atomic_t                atomic_ref_count;

}tracker_queue_t;

int tracker_queue_init(void );
int tracker_queue_done(void );

int tracker_queue_ref( struct request_queue* queue,    tracker_queue_t** ptracker_queue );
void tracker_queue_unref( tracker_queue_t* ptracker_queue );
int tracker_queue_find(    struct request_queue* queue, tracker_queue_t** ptracker_queue);

#endif //CONFIG_BLK_FILTER
