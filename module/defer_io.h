#pragma once

#include <linux/kref.h>
#include "snapstore_device.h"

struct defer_io_queue {
	struct list_head list;
	spinlock_t lock;

	atomic_t active_state;
	atomic_t in_queue_cnt;
};

struct defer_io {
	struct kref refcount;

	wait_queue_head_t queue_add_event;

	atomic_t queue_filling_count;
	wait_queue_head_t queue_throttle_waiter;

	dev_t original_dev_id;
	struct block_device *original_blk_dev;

	struct snapstore_device *snapstore_device;

	struct task_struct *dio_thread;

	struct defer_io_queue dio_queue;
};

int defer_io_create(dev_t dev_id, struct block_device *blk_dev, struct defer_io **pp_defer_io);
int defer_io_stop(struct defer_io *defer_io);

struct defer_io *defer_io_get_resource(struct defer_io *defer_io);
void defer_io_put_resource(struct defer_io *defer_io);

int defer_io_redirect_bio(struct defer_io *defer_io, struct bio *bio, void *tracker);
