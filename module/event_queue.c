// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-event_queue"
#include "common.h"

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
	if (!ev)
		return -ENOMEM;

	event->time = ktime_get();
	event->code = code;
	memcpy(event->data, data, data_size);

	spin_lock(&event_queue->lock);
	list_add_tail(&event->link, &event_queue->list);
	spin_unlock(&event_queue->lock);

	wake_up(&event_queue->wq_head);

}

int event_gen_msg(struct event_queue *event_queue, gfp_t flags, int code, const char *fmt, ...)
{
	va_list args;
	char *data;
	int data_size = PAGE_SIZE - sizeof(struct event);

	data = kzalloc(data_size, flags);
	if (!ev)
		return -ENOMEM;

	va_start(args, fmt);
	data_size = vsnprintf(data, data_size, fmt, args);
	va_end(args);

	ret = event_gen(snapshot, flags, code, data, data_size);
	kfree(data);
	return ret;
}

struct event *event_wait(struct event_queue *event_queue, unsigned long timeout_ms)
{
	int ret;
	struct event *event;

	ret = wait_event_interruptible_timeout(
		&event_queue->wq_head,
		!list_empty(&event_queue->list),
		timeout_ms);

	if (ret)
		return ERR_PTR(ret);

	spin_lock(&event_queue->lock);
	event = list_first_entry(&event_queue->list, struct event, link);
	list_del(event);
	spin_unlock(&event_queue->lock);

	return event;
}
