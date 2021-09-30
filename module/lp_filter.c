// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME "-lpflt: " fmt
#include <linux/module.h>
#include <linux/livepatch.h>
#include <linux/bio.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include "lp_filter.h"

/**
 * struct blk_filter - Description of the block device filter.
 * @link:
 * @dev_id:
 * @fops:
 * @ctx:
 *
 */
struct blk_filter {
	struct list_head link;
	dev_t dev_id;
#if defined(HAVE_BI_BDISK)
	struct gendisk *disk;
	u8 partno;
#endif
	const struct filter_operations *fops;
	void *ctx;
};

/* The list of filters for this block device */
static LIST_HEAD(bd_filters);

/* Lock the queue of block device to add or delete filter. */
DEFINE_PERCPU_RWSEM(bd_filters_lock);

/**
 * filter_write_lock() - Locks the processing of I/O requests for block device.
 * @bdev:
 *	Pointer to &struct block_device.
 *      The parameter is unused, but is present for compatibility with the
 * 	blk-filter from upstream.
 *
 * Locks block device the execution of the submit_bio_noacct() function for it.
 * To avoid calling a deadlock, do not call I/O operations after this lock.
 * To do this, using the memalloc_noio_save() function can be useful.
 *
 */
void filter_write_lock(struct block_device *__attribute__((__unused__))bdev)
{
	percpu_down_write(&bd_filters_lock);
};

/**
 * filter_write_unlock - Unlocks the processing of I/O requests for block device.
 * @bdev:
 *	Pointer to &struct block_device.
 *      The parameter is unused, but is present for compatibility with the
 * 	blk-filter from upstream.
 *
 * The submit_bio_noacct() function can be continued.
 */
void filter_write_unlock(struct block_device *__attribute__((__unused__))bdev)
{
	percpu_up_write(&bd_filters_lock);
};

static inline
struct blk_filter *filter_find(dev_t dev_id)
{
	struct blk_filter *flt;

	if (list_empty(&bd_filters))
		return NULL;

	list_for_each_entry(flt, &bd_filters, link) {
		if (dev_id == flt->dev_id)
			return flt;
	}
	return NULL;
}

#if defined(HAVE_BI_BDISK)
static inline
struct blk_filter *filter_find_by_disk(struct gendisk *disk, int partno)
{
	struct blk_filter *flt;

	if (list_empty(&bd_filters))
		return NULL;

	list_for_each_entry(flt, &bd_filters, link) {
		if ((disk == flt->disk) && (partno == flt->partno))
			return flt;
	}
	return NULL;
}
#endif

/**
 * filter_add - Attach a filter to original block device.
 * @bdev:
 * 	block device
 * @fops:
 * 	table of filter callbacks
 * @ctx:
 * 	Filter specific private data
 *
 * Before adding a filter, it is necessary to lock the processing
 * of bio requests of the original device by calling filter_write_lock().
 *
 * The filter_del() function allows to delete the filter from the block device.
 */
int filter_add(struct block_device *bdev,
	       const struct filter_operations *fops, void *ctx)
{
	struct blk_filter *flt;

	if (filter_find(bdev->bd_dev))
		return -EBUSY;

	flt = kzalloc(sizeof(struct blk_filter), GFP_NOIO);
	if (!flt)
		return -ENOMEM;

	INIT_LIST_HEAD(&flt->link);
	flt->dev_id = bdev->bd_dev;
#if defined(HAVE_BI_BDISK)
	flt->disk = bdev->bd_disk;
	flt->partno = bdev->bd_partno;
#endif
	flt->fops = fops;
	flt->ctx = ctx;
	list_add(&flt->link, &bd_filters);

	return 0;
}

/**
 * bdev_filter_del - Delete filter from the block device.
 * @bdev:
 * 	block device.
 * @filter_name:
 * 	unique filters name.
 *
 * Before deleting a filter, it is necessary to lock the processing
 * of bio requests of the device by calling filter_write_lock().
 *
 * The filter should be added using the bdev_filter_add() function.
 */
int filter_del(struct block_device *bdev)
{
	struct blk_filter *flt;

	flt = filter_find(bdev->bd_dev);
	if (!flt)
		return -ENOENT;

	if (flt->fops->detach_cb)
		flt->fops->detach_cb(flt->ctx);
	list_del(&flt->link);
	kfree(flt);

	return 0;
}

/**
 * filter_read_lock - Lock filters list, protecting them from changes.
 * @bdev:
 *	Pointer to &struct block_device.
 *      The parameter is unused, but is present for compatibility with the
 * 	blk-filter from upstream.
 *
 * The lock ensures that the filter will not be removed from the list until
 * the lock is removed.
 */
void filter_read_lock(struct block_device *__attribute__((__unused__))bdev)
{
	percpu_down_read(&bd_filters_lock);
}

/**
 * filter_read_unlock - Unlock filters list.
 * @bdev:
 *	Pointer to &struct block_device.
 *      The parameter is unused, but is present for compatibility with the
 * 	blk-filter from upstream.
 */
void filter_read_unlock(struct block_device *__attribute__((__unused__))bdev)
{
	percpu_up_read(&bd_filters_lock);
}

/**
 * filter_find_ctx - Get filters context value.
 * @dev_id:
 * 	Block device ID.
 *
 * Return &ctx value from &struct blk_filter or NULL.
 * NULL is returned if the filter was not found.
 *
 * Necessary to lock list of filters by calling filter_read_lock().
 */
void* filter_find_ctx(struct block_device *bdev)
{
	struct blk_filter *flt;

	flt = filter_find(bdev->bd_dev);
	if (flt)
		return flt->ctx;
	else
		return NULL;
}

static inline
enum flt_st filters_apply(struct bio *bio)
{
	struct blk_filter *flt;
	enum flt_st flt_st;

	if (bio->bi_opf & REQ_NOWAIT) {
		if (!percpu_down_read_trylock(&bd_filters_lock)) {
			bio_wouldblock_error(bio);
			return FLT_ST_COMPLETE;
		}
	} else
		percpu_down_read(&bd_filters_lock);

#if defined(HAVE_BI_BDISK)
	flt = filter_find_by_disk(bio->bi_disk, bio->bi_partno);
#else
	flt = filter_find(bio->bi_bdev->bd_dev);
#endif
	if (flt)
		flt_st = flt->fops->submit_bio_cb(bio, flt->ctx);
	else
		flt_st = FLT_ST_PASS;

	percpu_up_read(&bd_filters_lock);

	return flt_st;
}

#ifndef HAVE_SUBMIT_BIO_NOACCT
#error "Your kernel is too old for "KBUILD_MODNAME"."
#endif

#ifdef CONFIG_X86
#define CALL_INSTRUCTION_LENGTH	5
#else
#pragma error "Current CPU is not supported yet"
#endif

static
blk_qc_t (*submit_bio_noacct_notrace)(struct bio *) =
	(blk_qc_t (*)(struct bio *))((unsigned long)(submit_bio_noacct) +
	                             CALL_INSTRUCTION_LENGTH);

static
blk_qc_t notrace submit_bio_noacct_handler(struct bio *bio)
{
	if (!current->bio_list) {
		struct bio_list bio_list_on_stack[2];
		struct bio *new_bio;
		enum flt_st flt_st;

		bio_list_init(&bio_list_on_stack[0]);
		current->bio_list = bio_list_on_stack;
		barrier();

		flt_st = filters_apply(bio);

		current->bio_list = NULL;
		barrier();

		while ((new_bio = bio_list_pop(&bio_list_on_stack[0])))
			submit_bio_noacct_notrace(new_bio);

		if (flt_st == FLT_ST_COMPLETE)
			return BLK_QC_T_NONE;
	}

	return submit_bio_noacct_notrace(bio);
}

static
struct klp_func funcs[] = {
	{
		.old_name = "submit_bio_noacct",
		.new_func = submit_bio_noacct_handler,
	},
	{0}
};

static
struct klp_object objs[] = {
	{
		/* name being NULL means vmlinux */
		.funcs = funcs,
	},
	{0}
};

static
struct klp_patch patch = {
	.mod = THIS_MODULE,
	.objs = objs,
};

int filter_init(void)
{
	return klp_enable_patch(&patch);
}

void filter_done(void )
{
	percpu_free_rwsem(&bd_filters_lock);
}
