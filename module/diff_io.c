// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME "-diff-io: " fmt






static inline
void notify_fn(unsigned long error, void *context)
{
	struct chunk *chunk = context;

	cant_sleep();
	chunk->error = error;
	queue_work(system_wq, &chunk->notify_work);
	atomic_dec(&chunk->diff_area->pending_io_count);
}

// static it's should be nonstatic
void chunk_endio(struct bio *bio)
{


}
static
void diff_io_notify_cb(struct work_struct *work)
{

}


static inline
void diff_io_init(struct diff_io *diff_io, bool is_write)
{
	diff_io->error = 0;
	diff_io->is_write = is_write;
	atomic_set(&diff_io->endio_count, 0);
}

void diff_io_init_sync(struct diff_io *diff_io,  bool is_write)
{
	diff_io_init(diff_io, is_write);
	diff_io->is_sync_io = true;
	init_completion(&chunk->notify.sync.completion);
}

void diff_io_init_async(struct diff_io *diff_io, bool is_write,
			void (*notify_cb)(struct work_struct *work),
			void *ctx)
{
	diff_io_init(diff_io, is_write);
	diff_io->is_sync_io = false;
	INIT_WORK(&diff_io->notify.async.work, chunk_notify_store);
		diff_io->ctx = ctx;
}

int diff_io_do(struct diff_io *diff_io, struct diff_store *diff_store,
		struct diff_buffer *diff_buffer, bool is_write)
