// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME "-diff-io: " fmt
#include <linux/genhd.h>
#include <linux/slab.h>
#ifdef CONFIG_BLK_SNAP_DEBUG_MEMORY_LEAK
#include "memory_checker.h"
#endif
#include "diff_io.h"
#include "diff_buffer.h"

#ifdef CONFIG_BLK_SNAP_DEBUGLOG
#undef pr_debug
#define pr_debug(fmt, ...) printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
#endif

#define SECTORS_IN_PAGE (PAGE_SIZE / SECTOR_SIZE)

struct bio_set diff_io_bioset = { 0 };

int diff_io_init(void)
{
	return bioset_init(&diff_io_bioset, 64, 0,
			   BIOSET_NEED_BVECS | BIOSET_NEED_RESCUER);
}

void diff_io_done(void)
{
	bioset_exit(&diff_io_bioset);
}

static void diff_io_notify_cb(struct work_struct *work)
{
	struct diff_io_async *async =
		container_of(work, struct diff_io_async, work);

	might_sleep();
	async->notify_cb(async->ctx);
}

// static it's should be nonstatic
void diff_io_endio(struct bio *bio)
{
	struct diff_io *diff_io = bio->bi_private;

	cant_sleep();
	if (bio->bi_status != BLK_STS_OK)
		diff_io->error = -EIO;

	if (diff_io->is_sync_io)
		complete(&diff_io->notify.sync.completion);
	else
		queue_work(system_wq, &diff_io->notify.async.work);
	bio_put(bio);
}

static inline struct diff_io *diff_io_new(bool is_write, bool is_nowait)
{
	struct diff_io *diff_io;
	gfp_t gfp_mask = is_nowait ? (GFP_NOIO | GFP_NOWAIT) : GFP_NOIO;

	diff_io = kzalloc(sizeof(struct diff_io), gfp_mask);
	if (unlikely(!diff_io))
		return NULL;
#ifdef CONFIG_BLK_SNAP_DEBUG_MEMORY_LEAK
	memory_object_inc(memory_object_diff_io);
#endif
	diff_io->error = 0;
	diff_io->is_write = is_write;

	return diff_io;
}

struct diff_io *diff_io_new_sync(bool is_write)
{
	struct diff_io *diff_io;

	diff_io = diff_io_new(is_write, false);
	if (unlikely(!diff_io))
		return NULL;

	diff_io->is_sync_io = true;
	init_completion(&diff_io->notify.sync.completion);
	return diff_io;
}

struct diff_io *diff_io_new_async(bool is_write, bool is_nowait,
				  void (*notify_cb)(void *ctx), void *ctx)
{
	struct diff_io *diff_io;

	diff_io = diff_io_new(is_write, is_nowait);
	if (unlikely(!diff_io))
		return NULL;

	diff_io->is_sync_io = false;
	INIT_WORK(&diff_io->notify.async.work, diff_io_notify_cb);
	diff_io->notify.async.ctx = ctx;
	diff_io->notify.async.notify_cb = notify_cb;
	return diff_io;
}

static inline bool check_page_aligned(sector_t sector)
{
	return !(sector & ((1ULL << (PAGE_SHIFT - SECTOR_SHIFT)) - 1));
}

static inline unsigned short calc_page_count(sector_t sectors)
{
	return round_up(sectors, SECTOR_IN_PAGE) / SECTOR_IN_PAGE;
}


int diff_io_do(struct diff_io *diff_io, struct diff_region *diff_region,
	       struct diff_buffer *diff_buffer, const bool is_nowait,
	       const bool is_flush)
{
	struct page **current_page_ptr;
	struct bio *bio;
	unsigned short nr_iovecs;
	sector_t processed = 0;
	unsigned int op_flags =
		is_flush ? REQ_SYNC | REQ_FUA | REQ_PREFLUSH : 0;

	if (unlikely(!check_page_aligned(diff_region->sector))) {
		pr_err("Difference storage block should be aligned to PAGE_SIZE\n");
		return -EINVAL;
	}

	nr_iovecs = calc_page_count(diff_region->count);
	if (unlikely(nr_iovecs > diff_buffer->page_count)) {
		pr_err("The difference storage block is larger than the buffer size\n");
		return -EINVAL;
	}

	if (is_nowait) {
		bio = bio_alloc_bioset(GFP_NOIO | GFP_NOWAIT, nr_iovecs,
				       &diff_io_bioset);
		if (unlikely(!bio))
			return -EAGAIN;
	} else {
		bio = bio_alloc_bioset(GFP_NOIO, nr_iovecs, &diff_io_bioset);
		if (unlikely(!bio))
			return -ENOMEM;
	}

	bio_set_flag(bio, BIO_FILTERED);

	bio->bi_end_io = diff_io_endio;
	bio->bi_private = diff_io;
	bio_set_dev(bio, diff_region->bdev);
	bio->bi_iter.bi_sector = diff_region->sector;
	if (diff_io->is_write)
		bio_set_op_attrs(bio, REQ_OP_WRITE, op_flags);
	else
		bio_set_op_attrs(bio, REQ_OP_READ, op_flags);

	current_page_ptr = diff_buffer->pages;
	while (processed < diff_region->count) {
		sector_t bvec_len_sect;
		unsigned int bvec_len;

		bvec_len_sect = min_t(sector_t, SECTORS_IN_PAGE,
				      diff_region->count - processed);
		bvec_len = (unsigned int)(bvec_len_sect << SECTOR_SHIFT);

		if (bio_add_page(bio, *current_page_ptr, bvec_len, 0) == 0) {
			bio_put(bio);
			return -EFAULT;
		}

		current_page_ptr++;
		processed += bvec_len_sect;
	}

	submit_bio_noacct(bio);

	if (diff_io->is_sync_io)
		wait_for_completion_io(&diff_io->notify.sync.completion);

	return 0;
}
