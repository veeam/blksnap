/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <linux/workqueue.h>
#include <linux/completion.h>

struct diff_buffer;

/**
 * struct diff_region - Describes the location of the chunks data on
 *	difference storage.
 *
 */
struct diff_region {
	struct block_device *bdev;
	sector_t sector;
	sector_t count;
};

/* for synchronous IO */
struct diff_io_sync {
	struct completion completion;
};

/* for asynchronous IO */
struct diff_io_async {
	struct work_struct work;
	void (*notify_cb)(void *ctx);
	void *ctx;
};

struct diff_io {
	int error;
	atomic_t bio_count;
	bool is_write;
	bool is_sync_io;
	union {
		struct diff_io_sync sync;
		struct diff_io_async async;
	} notify;
};

int diff_io_init(void);
void diff_io_done(void);

static inline void diff_io_free(struct diff_io *diff_io)
{
	kfree(diff_io);
#ifdef CONFIG_BLK_SNAP_DEBUG_MEMORY_LEAK
	if (diff_io)
		memory_object_dec(memory_object_diff_io);
#endif
}

struct diff_io *diff_io_new_sync(bool is_write);
static inline struct diff_io *diff_io_new_sync_read(void)
{
	return diff_io_new_sync(false);
};
static inline struct diff_io *diff_io_new_sync_write(void)
{
	return diff_io_new_sync(true);
};

struct diff_io *diff_io_new_async(bool is_write, bool is_nowait,
				  void (*notify_cb)(void *ctx), void *ctx);
static inline struct diff_io *
diff_io_new_async_read(void (*notify_cb)(void *ctx), void *ctx, bool is_nowait)
{
	return diff_io_new_async(false, is_nowait, notify_cb, ctx);
};
static inline struct diff_io *
diff_io_new_async_write(void (*notify_cb)(void *ctx), void *ctx, bool is_nowait)
{
	return diff_io_new_async(true, is_nowait, notify_cb, ctx);
};

int diff_io_do(struct diff_io *diff_io, struct diff_region *diff_region,
	       struct diff_buffer *diff_buffer, const bool is_nowait);
