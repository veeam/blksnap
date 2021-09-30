/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <linux/types.h>
#include <linux/list.h>
#include <linux/blk_types.h>

/*
 * Each filter can skip the bio request or complete it,
 * or even redirect it to another block device.
 */
enum flt_st {
	FLT_ST_PASS,
	FLT_ST_COMPLETE,
};

struct filter_operations {
	enum flt_st (*submit_bio_cb)(struct bio *bio, void *ctx);
	void (*detach_cb)(void *ctx);
};

void filter_write_lock(struct block_device *bdev );
void filter_write_unlock(struct block_device *bdev );
void filter_read_lock(struct block_device *bdev );
void filter_read_unlock(struct block_device *bdev );

int filter_add(struct block_device *bdev, const struct filter_operations *fops, void *ctx);
int filter_del(struct block_device *bdev);

void* filter_find_ctx(struct block_device *bdev);

int filter_enable(void );
