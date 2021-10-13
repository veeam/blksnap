// SPDX-License-Identifier: GPL-2.0
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
		event = list_first_entry(&event_queue->list, struct event, link);
		list_del(&event->link);
		kfree(event);
	}
	spin_unlock(&event_queue->lock);
}

int event_gen(struct event_queue *event_queue, gfp_t flags, int code, const void *data, int data_size)
{
	struct event *event;

	event = kzalloc(sizeof(struct event) + data_size, flags);
	if (!event)
		return -ENOMEM;

	event->time = ktime_get();
	event->code = code;
	memcpy(event->data, data, data_size);

	pr_info("%s send event=%lld code=%d\n", __FUNCTION__, event->time, event->code);

	spin_lock(&event_queue->lock);
	list_add_tail(&event->link, &event_queue->list);
	spin_unlock(&event_queue->lock);

	wake_up(&event_queue->wq_head);
	return 0;
}
/*
int event_gen_msg(struct event_queue *event_queue, gfp_t flags, int code, const char *fmt, ...)
{
	va_list args;
	int ret;
	char *data;
	int data_size = PAGE_SIZE - sizeof(struct event);

	data = kzalloc(data_size, flags);
	if (!data)
		return -ENOMEM;

	va_start(args, fmt);
	data_size = vsnprintf(data, data_size, fmt, args);
	va_end(args);

	ret = event_gen(event_queue, flags, code, data, data_size);
	kfree(data);
	return ret;
}
*/
struct event *event_wait(struct event_queue *event_queue, unsigned long timeout_ms)
{
	int ret;

	ret = wait_event_interruptible_timeout(
		event_queue->wq_head,
		!list_empty(&event_queue->list),
		timeout_ms);

	if (ret == 1) {
		struct event *event;

		spin_lock(&event_queue->lock);
		event = list_first_entry(&event_queue->list, struct event, link);
		list_del(&event->link);
		spin_unlock(&event_queue->lock);

		pr_info("%s received event=%lld code=%d\n", __FUNCTION__, event->time, event->code);
		return event;
	}
	if (ret == 0) {
		pr_info("%s - timeout\n", __FUNCTION__);
		return ERR_PTR(-ENOENT);
	}

	if (ret == -ERESTARTSYS) {
		pr_info("%s - interrupted\n", __FUNCTION__);
		return ERR_PTR(-EINTR);
	}

	pr_err("Failed to wait event. errno=%d\n", abs(ret));
	return ERR_PTR(ret);
}

