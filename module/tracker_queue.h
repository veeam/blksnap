#pragma once
#include "container_spinlocking.h"

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
