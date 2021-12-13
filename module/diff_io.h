/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

struct diff_store;
struct diff_buffer;

struct diff_io_sync
{
	struct completion completion;	// for synch IO
};

struct diff_io_async
{
	struct work_struct work;	// for async IO
	void (*notify_cb)(void *ctx);
	struct void *ctx;
};

struct diff_io {
	int error;
	atomic_t endio_count;
	bool is_write;
	bool is_sync_io;
	union notify {
		struct diff_io_sync sync;
		struct diff_io_async async;
	};
};



void diff_io_init_sync(struct diff_io *diff_io,  bool is_write);
void diff_io_init_async(struct diff_io *diff_io, bool is_write,
			void (*notify_cb)(void *ctx),
			void *ctx);

int diff_io_do(struct diff_io *diff_io, struct diff_store *diff_store,
		struct diff_buffer *diff_buffer, bool is_write);
static inline
int diff_io_read(struct diff_io *diff_io, struct diff_store *diff_store,
		struct diff_buffer *diff_buffer)
{
	return diff_io_do(diff_io, diff_store, diff_buffer, false);
};
static inline
int diff_io_write(struct diff_io *diff_io, struct diff_store *diff_store,
		struct diff_buffer *diff_buffer)
{
	return diff_io_do(diff_io, diff_store, diff_buffer, true);
};
