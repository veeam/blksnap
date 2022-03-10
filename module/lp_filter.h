/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <linux/types.h>
#include <linux/list.h>
#include <linux/blk_types.h>

#define BDEV_FILTER_NAME_MAX_LENGTH 31

enum bdev_filter_result {
	bdev_filter_skip = 0,
	bdev_filter_pass,
	bdev_filter_repeat,
};

struct bdev_filter_operations {
	enum bdev_filter_result (*submit_bio_cb)(struct bio *bio, void *ctx);
	void (*detach_cb)(void *ctx);
};

void bdev_filter_write_lock(struct block_device *bdev);
void bdev_filter_write_unlock(struct block_device *bdev);
void bdev_filter_read_lock(struct block_device *bdev);
void bdev_filter_read_unlock(struct block_device *bdev);

int bdev_filter_add(struct block_device *bdev, const char *name,
		    const struct bdev_filter_operations *fops, void *ctx);
int bdev_filter_del(struct block_device *bdev, const char *name);

void *bdev_filter_get_ctx(struct block_device *bdev, const char *name);
