#pragma once

#include <linux/kref.h>
#include "snapstore_device.h"

typedef struct defer_io_queue {
	struct list_head list;
	spinlock_t lock;

	atomic_t active_state;
	atomic_t in_queue_cnt;
} defer_io_queue_t;

typedef struct defer_io_s {
	struct kref refcount;

	wait_queue_head_t queue_add_event;

	atomic_t queue_filling_count;
	wait_queue_head_t queue_throttle_waiter;

	dev_t original_dev_id;
	struct block_device *original_blk_dev;

	snapstore_device_t *snapstore_device;

	struct task_struct *dio_thread;

	defer_io_queue_t dio_queue;

} defer_io_t;

int defer_io_create(dev_t dev_id, struct block_device *blk_dev, defer_io_t **pp_defer_io);
int defer_io_stop(defer_io_t *defer_io);

defer_io_t *defer_io_get_resource(defer_io_t *defer_io);
void defer_io_put_resource(defer_io_t *defer_io);

int defer_io_redirect_bio(defer_io_t *defer_io, struct bio *bio, void *tracker);
