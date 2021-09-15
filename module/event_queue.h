/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <linux/types.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

/**
 * struct event - An event to pass it to the user-space.
 */
struct event {
	struct list_head link;
	ktime_t time;
	int code;
	size_t data_size;
	char data[1]; /* up to PAGE_SIZE - sizeof(struct blk_snap_snapshot_event) */
};

/**
 * struct event_queue - A queue of &struct event.
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
/*
int event_gen_msg(struct event_queue *event_queue, gfp_t flags, int code,
		  const char *fmt, ...);
*/
struct event *event_wait(struct event_queue *event_queue, unsigned long timeout_ms);
