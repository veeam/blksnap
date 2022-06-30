/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BLK_SNAP_EVENT_QUEUE_H
#define __BLK_SNAP_EVENT_QUEUE_H

#include <linux/types.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

/**
 * struct event - An event to be passed to the user space.
 * @link:
 *	The list header allows to combine events from the queue.
 * @time:
 *	A timestamp indicates when an event occurred.
 * @code:
 *	Event code.
 * @data_size:
 *	The number of bytes in the event data array.
 * @data:
 *	An array of event data.
 *
 * Events can be different, so they contain different data. The size of the
 * data array is not defined exactly, but it has limitations. The size of
 * the event structure may exceed the PAGE_SIZE.
 */
struct event {
	struct list_head link;
	ktime_t time;
	int code;
	int data_size;
	char data[1]; /* up to PAGE_SIZE - sizeof(struct blk_snap_snapshot_event) */
};

/**
 * struct event_queue - A queue of &struct event.
 * @list:
 *	Linked list for storing events.
 * @lock:
 *	Spinlock allows to guarantee safety of the linked list.
 * @wq_head:
 *	A wait queue allows to put a user thread in a waiting state until
 *	an event appears in the linked list.
 */
struct event_queue {
	struct list_head list;
	spinlock_t lock;
	struct wait_queue_head wq_head;
};

void event_queue_init(struct event_queue *event_queue);
void event_queue_done(struct event_queue *event_queue);

int event_gen(struct event_queue *event_queue, gfp_t flags, int code,
	      const void *data, int data_size);
struct event *event_wait(struct event_queue *event_queue,
			 unsigned long timeout_ms);
static inline void event_free(struct event *event)
{
	kfree(event);
#ifdef CONFIG_BLK_SNAP_DEBUG_MEMORY_LEAK
	if (event)
		memory_object_dec(memory_object_event);
#endif
};
#endif /* __BLK_SNAP_EVENT_QUEUE_H */
