// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-lpflt"
#include "common.h"
#include "lp-filter.h"
#include <linux/livepatch.h>

/* The list of filters for this block device */
static LIST_HEAD(bd_filters);

/* Lock the queue of block device to add or delete filter. */
static DEFINE_PERCPU_RWSEM(bd_filters_lock);

/**
 * filters_write_lock() - Locks the processing of I/O requests for block device.
 *
 * Locks block device the execution of the submit_bio_noacct() function for it.
 * To avoid calling a deadlock, do not call I/O operations after this lock.
 * To do this, using the memalloc_noio_save() function can be useful.
 *
 * If successful, returns a pointer to the block device structure.
 * Returns an error code when an error occurs.
 */
void filters_write_lock(void )
{
	percpu_down_write(&bd_filters_lock);
};

/**
 * bdev_filter_lock - Unlocks the processing of I/O requests for block device.

 * @bdev:
 *	pointer to block device structure
 *
 * The submit_bio_noacct() function can be continued.
 */
void filters_write_unlock(void )
{
	percpu_up_write(&bd_filters_lock);
};

static inline 
#if defined(HAVE_BI_BDEV)
struct blk_filter *filter_find(dev_t dev_id)
#elif defined(HAVE_BI_BDISK)
struct blk_filter *filter_find(int major, int partno)
#endif
{
	struct blk_filter *flt;

	if (list_empty(&bd_filters))
		return NULL;
	
	list_for_each_entry(flt, &bd_filters, list) {
#if defined(HAVE_BI_BDEV)
		if (dev_id == flt->dev_id)
			return flt;
#elif defined(HAVE_BI_BDISK)
		if ((major == flt->major) && (partno == flt->partno))
			return flt;
#endif
	}
	return NULL;
}

static 
int __filter_add(struct block_device *bdev,
		 const struct filter_operations *fops, void *ctx)
{
	struct blk_filter *flt;

#if defined(HAVE_BI_BDEV)
	if (filter_find(bdev->bd_dev))
#elif defined(HAVE_BI_BDISK)
	if (filter_find(bdev->bd_disk->major, bdev->bd_partno))
#endif
		return -EBUSY;

	flt = kzalloc(sizeof(struct blk_filter), GFP_NOIO);
	if (!flt)
		return -ENOMEM;

#if defined(HAVE_BI_BDEV)
	flt->dev_id = bdev->bd_dev;
#elif defined(HAVE_BI_BDISK)
	flt->major = bdev->bd_disk->major;
	flt->bi_partno = bdev->bi_partno;
#endif
	flt->fops = fops;
	flt->ctx = ctx;
	list_add(&flt->list, &bdev->bd_filters);

	return 0;
}

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
 * of bio requests of the original device by calling filters_write_lock().
 *
 * The filter_del() function allows to delete the filter from the block device.
 */
int filter_add(struct block_device *bdev,
	       const struct filter_operations *fops, void *ctx)
{
	int ret;
	unsigned int current_flag;

	filters_write_lock();
	current_flag = memalloc_noio_save();
		
	ret = __filter_add(bdev, fops, ctx);

	memalloc_noio_restore(current_flag);
	filters_write_unlock();

	return ret;
}

static
int __filter_del(struct block_device *bdev)
{
	struct blk_filter *flt;

#if defined(HAVE_BI_BDEV)
	flt = filter_find(bdev->bd_dev);
#elif defined(HAVE_BI_BDISK)
	flt = filter_find(bdev->bd_disk->major, bdev->bd_partno);
#endif
	if (!flt)
		return -ENOENT;

	if (flt->fops->detach_cb)
		flt->fops->detach_cb(flt->ctx);
	list_del(&flt->list);
	kfree(flt);

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
 * of bio requests of the device by calling bdev_filter_lock().
 *
 * The filter should be added using the bdev_filter_add() function.
 */
int filter_del(struct block_device *bdev)
{
	int ret;
	unsigned int current_flag;

	filters_write_lock();
	current_flag = memalloc_noio_save();

	ret = __filter_del(bdev);

	memalloc_noio_restore(current_flag);
	filters_write_unlock();

	return ret;
}

void filters_read_lock(void )
{
	percpu_down_read(&bd_filters_lock);	
}

void filters_read_unlock(void )
{
	percpu_up_read(&bd_filters_lock);
}

static inline
bool filters_read_lock_for_bio(struct bio *bio)
{
	if (bio->bi_opf & REQ_NOWAIT)
		return percpu_down_read_trylock(&bd_filters_lock);

	percpu_down_read(&bd_filters_lock);
	return true;
}

static inline
struct blk_filter *flt filter_find_by_bio(const struct bio *bio)
{
#if defined(HAVE_BI_BDEV)
	return filter_find(bio->bi_bdev->bd_dev);
#elif defined(HAVE_BI_BDISK)
	return filter_find(bio->bi_bdisk->major, bio->bi_partno);
#endif
}

static
int filters_apply(const struct bio *bio)
{
	struct blk_filter *flt;
	int status;

	if (unlikely(!filters_read_lock_for_bio(bio))) {
		bio_wouldblock_error(bio);
		return FLT_ST_COMPLETE;
	}

	flt = filter_find_by_bio(bio);
	if (flt)
		status = flt->fops->submit_bio_cb(bio, flt->ctx);

	filters_read_unlock();

	return status;
}

#ifndef HAVE_SUBMIT_BIO_NOACCT
#error "Your kernel is too old for "KBUILD_MODNAME"."
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
		int status;

		bio_list_init(&bio_list_on_stack[0]);
		current->bio_list = bio_list_on_stack;

		status = filters_apply(bio);

		current->bio_list = NULL;

		while ((new_bio = bio_list_pop(&bio_list_on_stack[0])))
			submit_bio_noacct_notrace(new_bio);

		if (status == FLT_ST_COMPLETE)
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

int filter_enable(void)
{
	return klp_enable_patch(&patch);
}
