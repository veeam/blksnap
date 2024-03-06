// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Veeam Software Group GmbH */
#define pr_fmt(fmt) KBUILD_MODNAME "-event_queue: " fmt

#include <linux/slab.h>
#include <linux/sched.h>
#include "event_queue.h"

void event_queue_init(struct event_queue *event_queue)
{
	INIT_LIST_HEAD(&event_queue->list);
	spin_lock_init(&event_queue->lock);
	init_waitqueue_head(&event_queue->wq_head);
}

void event_queue_done(struct event_queue *event_queue)
{
	struct event *event;

	spin_lock(&event_queue->lock);
	while (!list_empty(&event_queue->list)) {
		event = list_first_entry(&event_queue->list, struct event,
					 link);
		list_del(&event->link);
		event_free(event);
	}
	spin_unlock(&event_queue->lock);
}

int event_gen(struct event_queue *event_queue, int code,
	      const void *data, int data_size)
{
	struct event *event;

	event = kzalloc(sizeof(struct event) + data_size + 1, GFP_KERNEL);
	if (!event)
		return -ENOMEM;

	event->time = ktime_get();
	event->code = code;
	event->data_size = data_size;
	memcpy(event->data, data, data_size);

	pr_debug("Generate event: time=%lld code=%d data_size=%d\n",
		 event->time, event->code, event->data_size);

	spin_lock(&event_queue->lock);
	list_add_tail(&event->link, &event_queue->list);
	spin_unlock(&event_queue->lock);

	wake_up(&event_queue->wq_head);
	return 0;
}

struct event *event_wait(struct event_queue *event_queue,
			 unsigned long timeout_ms)
{
	int ret;

	ret = wait_event_interruptible_timeout(event_queue->wq_head,
				!list_empty(&event_queue->list), timeout_ms);
	if (ret >= 0) {
		struct event *event = ERR_PTR(-ENOENT);

		spin_lock(&event_queue->lock);
		if (!list_empty(&event_queue->list)) {
			event = list_first_entry(&event_queue->list,
						 struct event, link);
			list_del(&event->link);
		}
		spin_unlock(&event_queue->lock);
		return event;
	}
	if (ret == -ERESTARTSYS) {
		pr_debug("event waiting interrupted\n");
		return ERR_PTR(-EINTR);
	}

	pr_err("Failed to wait event. errno=%d\n", abs(ret));
	return ERR_PTR(ret);
}
