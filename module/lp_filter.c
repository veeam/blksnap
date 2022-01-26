// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/livepatch.h>
#include <linux/bio.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>

#include "lp_filter.h"
#include "version.h"
/**
 * struct bdev_filter - Description of the block device filter.
 * @link:
 * @dev_id:
 * @fops:
 * @ctx:
 *
 */
struct bdev_filter {
	struct list_head link;
	dev_t dev_id;
	char name[BDEV_FILTER_NAME_MAX_LENGTH];
#if defined(HAVE_BI_BDISK)
	struct gendisk *disk;
	u8 partno;
#endif
	const struct bdev_filter_operations *fops;
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
 *
 * Locks block device the execution of the submit_bio_noacct() function for it.
 * To avoid calling a deadlock, do not call I/O operations after this lock.
 * To do this, using the memalloc_noio_save() function can be useful.
 *
 */
void bdev_filter_write_lock(struct block_device *__attribute__((__unused__))bdev)
{
	percpu_down_write(&bd_filters_lock);
};
EXPORT_SYMBOL(bdev_filter_write_lock);

/**
 * filter_write_unlock - Unlocks the processing of I/O requests for block device.
 * @bdev:
 *	Pointer to &struct block_device.
 *
 * The submit_bio_noacct() function can be continued.
 */
void bdev_filter_write_unlock(struct block_device *__attribute__((__unused__))bdev)
{
	percpu_up_write(&bd_filters_lock);
};
EXPORT_SYMBOL(bdev_filter_write_unlock);

static inline
struct bdev_filter *filter_find(dev_t dev_id, const char *name)
{
	struct bdev_filter *flt;

	if (list_empty(&bd_filters))
		return NULL;

	list_for_each_entry(flt, &bd_filters, link) {
		if ((dev_id == flt->dev_id) &&
		    (strncmp(name, flt->name, BDEV_FILTER_NAME_MAX_LENGTH) == 0)) {
			return flt;
		}
	}
	return NULL;
}

#if defined(HAVE_BI_BDISK)
static inline
struct bdev_filter *filter_find_by_disk(struct gendisk *disk, int partno)
{
	struct bdev_filter *flt;

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
 * bdev_filter_add - Attach a filter to original block device.
 * @bdev:
 * 	block device
 * @fops:
 * 	table of filter callbacks
 * @ctx:
 * 	Filter specific private data
 *
 * Before adding a filter, it is necessary to lock the processing
 * of bio requests of the original device by calling bdev_filter_write_lock().
 *
 * The bdev_filter_del() function allows to delete the filter from the block device.
 */
int bdev_filter_add(struct block_device *bdev, const char *name,
	       const struct bdev_filter_operations *fops, void *ctx)
{
	struct bdev_filter *flt;

	if (strlen(name) >= BDEV_FILTER_NAME_MAX_LENGTH)
		return -EINVAL;

	if (filter_find(bdev->bd_dev, name))
		return -EBUSY;

	flt = kzalloc(sizeof(struct bdev_filter), GFP_NOIO);
	if (!flt)
		return -ENOMEM;

	INIT_LIST_HEAD(&flt->link);
	flt->dev_id = bdev->bd_dev;
	strncpy(flt->name, name, BDEV_FILTER_NAME_MAX_LENGTH);
#if defined(HAVE_BI_BDISK)
	flt->disk = bdev->bd_disk;
	flt->partno = bdev->bd_partno;
#endif
	flt->fops = fops;
	flt->ctx = ctx;
	list_add(&flt->link, &bd_filters);

	return 0;
}
EXPORT_SYMBOL(bdev_filter_add);

/**
 * bdev_bdev_filter_del - Delete filter from the block device.
 * @bdev:
 * 	block device.
 * @filter_name:
 * 	unique filters name.
 *
 * Before deleting a filter, it is necessary to lock the processing
 * of bio requests of the device by calling bdev_filter_write_lock().
 *
 * The filter should be added using the bdev_bdev_filter_add() function.
 */
int bdev_filter_del(struct block_device *bdev, const char *name)
{
	struct bdev_filter *flt;

	flt = filter_find(bdev->bd_dev, name);
	if (!flt)
		return -ENOENT;

	if (flt->fops->detach_cb)
		flt->fops->detach_cb(flt->ctx);
	list_del(&flt->link);
	kfree(flt);

	return 0;
}
EXPORT_SYMBOL(bdev_filter_del);

/**
 * filter_read_lock - Lock filters list, protecting them from changes.
 * @bdev:
 *	Pointer to &struct block_device.
 *
 * The lock ensures that the filter will not be removed from the list until
 * the lock is removed.
 */
void bdev_filter_read_lock(struct block_device *__attribute__((__unused__))bdev)
{
	percpu_down_read(&bd_filters_lock);
}
EXPORT_SYMBOL(bdev_filter_read_lock);

/**
 * filter_read_unlock - Unlock filters list.
 * @bdev:
 *	Pointer to &struct block_device.
 */
void bdev_filter_read_unlock(struct block_device *__attribute__((__unused__))bdev)
{
	percpu_up_read(&bd_filters_lock);
}
EXPORT_SYMBOL(bdev_filter_read_unlock);

/**
 * bdev_filter_get_ctx - Get filters context value.
 * @dev_id:
 * 	Block device ID.
 *
 * Return &ctx value from &struct bdev_filter or NULL.
 * NULL is returned if the filter was not found.
 *
 * Necessary to lock list of filters by calling bdev_filter_read_lock().
 */
void* bdev_filter_get_ctx(struct block_device *bdev, const char *name)
{
	struct bdev_filter *flt;

	flt = filter_find(bdev->bd_dev, name);
	if (flt)
		return flt->ctx;
	else
		return NULL;
}
EXPORT_SYMBOL(bdev_filter_get_ctx);

static inline
bool bdev_filters_apply(struct bio *bio)
{
	struct bdev_filter *flt;
	bool pass = true;

	if (bio->bi_opf & REQ_NOWAIT) {
		if (!percpu_down_read_trylock(&bd_filters_lock)) {
			bio_wouldblock_error(bio);
			return false;
		}
	} else
		percpu_down_read(&bd_filters_lock);


	if (!list_empty(&bd_filters)) {
		list_for_each_entry(flt, &bd_filters, link) {
#if defined(HAVE_BI_BDISK)
			if ((bio->bi_disk == flt->disk) && (bio->bi_partno == flt->partno))
#else
			if ((bio->bi_bdev->bd_dev == flt->dev_id) )
#endif
				pass = flt->fops->submit_bio_cb(bio, flt->ctx);
			if (!pass)
				break;
		}
	}

	percpu_up_read(&bd_filters_lock);

	return pass;
}

#ifdef CONFIG_X86
#define CALL_INSTRUCTION_LENGTH	5
#else
#pragma error "Current CPU is not supported yet"
#endif

#if defined(HAVE_QC_SUBMIT_BIO_NOACCT)
static
blk_qc_t (*submit_bio_noacct_notrace)(struct bio *) =
	(blk_qc_t (*)(struct bio *))((unsigned long)(submit_bio_noacct) +
				     CALL_INSTRUCTION_LENGTH);
#elif defined(HAVE_VOID_SUBMIT_BIO_NOACCT)
static
void (*submit_bio_noacct_notrace)(struct bio *) =
	(void (*)(struct bio *))((unsigned long)(submit_bio_noacct) +
				     CALL_INSTRUCTION_LENGTH);
#else
#error "Your kernel is too old for this module."
#endif

#ifdef BDEV_FILTER_SYNC
#pragma message "Have BDEV_FILTER_SYNC"
void notrace bdev_filter_submit_bio_noacct(struct bio *bio)
{
	submit_bio_noacct_notrace(bio);
}
EXPORT_SYMBOL(bdev_filter_submit_bio_noacct);
#endif

#if defined(HAVE_QC_SUBMIT_BIO_NOACCT)
static
blk_qc_t notrace submit_bio_noacct_handler(struct bio *bio)
#else
static
void notrace submit_bio_noacct_handler(struct bio *bio)
#endif
{
	if (!current->bio_list) {
		bool pass;

#ifdef BDEV_FILTER_SYNC
		pass = bdev_filters_apply(bio);
#else
		struct bio_list bio_list_on_stack[2];
		struct bio *new_bio;

		bio_list_init(&bio_list_on_stack[0]);
		current->bio_list = bio_list_on_stack;
		barrier();

		pass = bdev_filters_apply(bio);

		current->bio_list = NULL;
		barrier();

		while ((new_bio = bio_list_pop(&bio_list_on_stack[0]))) {
			/*
			 * The result from submitting a bio from the filter
			 * itself does not need to be processed,
			 * even if this function has a return code.
			 */
			submit_bio_noacct_notrace(new_bio);
		}
#endif
		if (!pass) {
#if defined(HAVE_QC_SUBMIT_BIO_NOACCT)
			return BLK_QC_T_NONE;
#elif defined(HAVE_VOID_SUBMIT_BIO_NOACCT)
			return;
#else
#error "Your kernel is too old for this module."
#endif
		}
	}

#if defined(HAVE_QC_SUBMIT_BIO_NOACCT)
	return submit_bio_noacct_notrace(bio);
#elif defined(HAVE_VOID_SUBMIT_BIO_NOACCT)
	submit_bio_noacct_notrace(bio);
#else
#error "Your kernel is too old for this module."
#endif
}


static
struct klp_func funcs[] = {
	{
		.old_name = "submit_bio_noacct",
		.new_func = submit_bio_noacct_handler,
	},
	{0}
};

#ifdef BDEV_FILTER_LOCKAPI
static
void post_patch(struct klp_object *obj)
{
	pr_warn("Filter is ready for using\n");
	percpu_up_read(&bd_filters_lock);
}

static
void pre_unpatch(struct klp_object *obj)
{
	pr_warn("Filter will be turned off now\n");
}

static
struct klp_callbacks callbacks
{
	.pre_patch = NULL,
	.post_patch = post_patch,
	.pre_unpatch = pre_unpatch,
	.post_unpatch = NULL,
	.post_unpatch_enabled = false,
};
#endif

static
struct klp_object objs[] = {
	{
		/* name being NULL means vmlinux */
		.funcs = funcs,
#ifdef BDEV_FILTER_LOCKAPI
		.callbacks = callbacks,
#endif
	},
	{0}
};

static
struct klp_patch patch = {
	.mod = THIS_MODULE,
	.objs = objs,
};

int bdev_filter_init(void)
{
#ifdef BDEV_FILTER_LOCKAPI
	percpu_down_read(&bd_filters_lock);
#endif
	return klp_enable_patch(&patch);
}

/*
 * For standalone only:
 * Before unload module livepatch should be detached.
 *
 * echo 0 > /sys/kernel/livepatch/bdev_filter/enabled
 */
void bdev_filter_done(void )
{
	percpu_free_rwsem(&bd_filters_lock);
}

module_init(bdev_filter_init);
module_exit(bdev_filter_done);

MODULE_DESCRIPTION("Block Device Filter kernel module");
MODULE_VERSION(VERSION_STR);
MODULE_AUTHOR("Veeam Software Group GmbH");
MODULE_LICENSE("GPL");
MODULE_INFO(livepatch, "Y");
/* Allow to be loaded on OpenSUSE/SLES */
MODULE_INFO(supported, "external");
