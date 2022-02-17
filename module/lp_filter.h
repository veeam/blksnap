/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <linux/types.h>
#include <linux/list.h>
#include <linux/blk_types.h>

#define BDEV_FILTER_NAME_MAX_LENGTH 32

struct bdev_filter_operations {
	bool (*submit_bio_cb)(struct bio *bio, void *ctx);
	void (*detach_cb)(void *ctx);
};

#ifdef BDEV_FILTER_SYNC
void bdev_filter_submit_bio_noacct(struct bio *bio);
#endif

void bdev_filter_write_lock(struct block_device *bdev);
void bdev_filter_write_unlock(struct block_device *bdev);
void bdev_filter_read_lock(struct block_device *bdev);
void bdev_filter_read_unlock(struct block_device *bdev);

int bdev_filter_add(struct block_device *bdev, const char *name,
		    const struct bdev_filter_operations *fops, void *ctx);
int bdev_filter_del(struct block_device *bdev, const char *name);

void *bdev_filter_get_ctx(struct block_device *bdev, const char *name);
