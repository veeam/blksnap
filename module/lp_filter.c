// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/livepatch.h>
#include <linux/bio.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/list.h>

#include "lp_filter.h"
#include "version.h"

struct bdev_extension {
	struct list_head link;

	dev_t dev_id;
#if defined(HAVE_BI_BDISK)
	struct gendisk *disk;
	u8 partno;
#endif

	struct bdev_filter *bd_filters[bdev_filter_alt_end];
	spinlock_t bd_filters_lock;
};

/* The list of extensions for this block device */
static LIST_HEAD(bdev_extension_list);

/* Lock the queue of block device to add or delete extension. */
static DEFINE_SPINLOCK(bdev_extension_list_lock);

static inline struct bdev_extension *bdev_extension_find(dev_t dev_id)
{
	struct bdev_extension *ext;

	if (list_empty(&bdev_extension_list))
		return NULL;

	list_for_each_entry (ext, &bdev_extension_list, link)
		if (dev_id == ext->dev_id)
			return ext;

	return NULL;
}

#if defined(HAVE_BI_BDISK)
static inline struct bdev_extension *bdev_extension_find_part(struct gendisk *disk,
							 u8 partno)
{
	struct bdev_extension *ext;

	if (list_empty(&bdev_extension_list))
		return NULL;

	list_for_each_entry (ext, &bdev_extension_list, link)
		if ((disk == ext->disk) && (partno == ext->partno))
			return ext;

	return NULL;
}
#endif

static inline struct bdev_extension *bdev_extension_append(struct block_device *bdev)
{
	struct bdev_extension *ext;
	struct bdev_extension *ext_new;

	ext_new = kzalloc(sizeof(struct bdev_extension), GFP_NOIO);
	if (!ext_new)
		return NULL;

	ext_new->dev_id = bdev->bd_dev;
#if defined(HAVE_BI_BDISK)
	ext_new->disk = bdev->bd_disk;
	ext_new->partno = bdev->bd_partno;
#endif

	spin_lock(&bdev_extension_list_lock);
	ext = bdev_extension_find(bdev->bd_dev);
	if (!ext)
		list_add_tail(&ext_new->link, &bdev_extension_list);
	spin_unlock(&bdev_extension_list_lock);

	if (!ext)
		return ext_new;

	kfree(ext_new);
	return ext;
}

/**
 * bdev_filter_add - Attach a filter to original block device.
 * @bdev:
 * 	block device
 * @name:
 *	Name of the block device filter.
 * @altitude:
 *	Altituda number of the block device filter.
 * @flt:
 *	Pointer to the filter structure.
 *
 * Before adding a filter, it is necessary to initialize &struct bdev_filter.
 *
 * The bdev_filter_detach() function allows to detach the filter from the block
 * device.
 *
 * Return:
 * 0 - OK
 * -EALREADY - a filter with this name already exists
 */
int bdev_filter_attach(struct block_device *bdev, const char *name,
		       const enum bdev_filter_altitudes altitude,
		       struct bdev_filter *flt)
{
	int ret = 0;
	struct bdev_extension *ext;

	pr_info("Attach block device filter");

	ext = bdev_extension_append(bdev);
	if (!ext)
		return -ENOMEM;

	spin_lock(&ext->bd_filters_lock);
	if (ext->bd_filters[altitude])
		ret = -EBUSY;
	else
		ext->bd_filters[altitude] = flt;
	spin_unlock(&ext->bd_filters_lock);

	if (!ret)
		pr_info("block device filter '%s' has been attached to %d:%d",
			name, MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev));
	return ret;
}
EXPORT_SYMBOL(bdev_filter_attach);

/**
 * bdev_bdev_filter_del - Detach a filter from the block device.
 * @bdev:
 * 	block device.
 * @name:
 *	Name of the block device filter.
 * @altitude:
 *	Altituda number of the block device filter.
 *
 * The filter should be added using the bdev_filter_attach() function.
 *
 * Return:
 * 0 - OK
 * -ENOENT - the filter was not found in the linked list
 */
int bdev_filter_detach(struct block_device *bdev, const char *name,
		       const enum bdev_filter_altitudes altitude)
{
	struct bdev_extension *ext;
	struct bdev_filter *flt;

	pr_info("Detach block device filter");

	spin_lock(&bdev_extension_list_lock);
	ext = bdev_extension_find(bdev->bd_dev);
	spin_unlock(&bdev_extension_list_lock);
	if (!ext)
		return -ENOENT;

	spin_lock(&ext->bd_filters_lock);
	flt = ext->bd_filters[altitude];
	if (flt)
		ext->bd_filters[altitude] = NULL;
	spin_unlock(&ext->bd_filters_lock);

	if (!flt)
		return -ENOENT;

	bdev_filter_put(flt);
	pr_info("block device filter '%s' has been detached from %d:%d",
		name, MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev));
	return 0;
}
EXPORT_SYMBOL(bdev_filter_detach);

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
struct bdev_filter *bdev_filter_get_by_altitude(struct block_device *bdev,
				const enum bdev_filter_altitudes altitude)
{
	struct bdev_extension *ext;
	struct bdev_filter *flt = NULL;

	spin_lock(&bdev_extension_list_lock);
	ext = bdev_extension_find(bdev->bd_dev);
	spin_unlock(&bdev_extension_list_lock);
	if (!ext)
		return NULL; //-ENOENT;

	spin_lock(&ext->bd_filters_lock);
	flt = ext->bd_filters[altitude];
	bdev_filter_get(flt);
	spin_unlock(&ext->bd_filters_lock);

	return flt;
}
EXPORT_SYMBOL(bdev_filter_get_by_altitude);

static inline enum bdev_filter_result bdev_filters_apply(struct bio *bio, enum bdev_filter_altitudes *paltitude)
{
	enum bdev_filter_result result = bdev_filter_pass;
	struct bdev_extension *ext;
	struct bdev_filter *flt;

	spin_lock(&bdev_extension_list_lock);
#if defined(HAVE_BI_BDISK)
	ext = bdev_extension_find_part(bio->bi_disk, bio->bi_partno);
#else
	ext = bdev_extension_find(bio->bi_bdev->bd_dev);
#endif
	spin_unlock(&bdev_extension_list_lock);
	if (!ext)
		return result;


	spin_lock(&ext->bd_filters_lock);
	while (*paltitude < bdev_filter_alt_end) {
		flt = ext->bd_filters[*paltitude];
		if (!flt) {
			*paltitude = *paltitude + 1;
			continue;
		}

		bdev_filter_get(flt);
		spin_unlock(&ext->bd_filters_lock);

		result = flt->fops->submit_bio_cb(bio, flt);

		switch (result) {
		case bdev_filter_skip:
		case bdev_filter_repeat:
			*paltitude = bdev_filter_alt_end;
			break;
		case bdev_filter_pass:
			*paltitude = *paltitude + 1;
			break;
		case bdev_filter_redirect:
			*paltitude = bdev_filter_alt_unidentified;
			break;
		}

		bdev_filter_put(flt);
		spin_lock(&ext->bd_filters_lock);
	};
	spin_unlock(&ext->bd_filters_lock);

	return result;
}

#ifdef CONFIG_X86
#define CALL_INSTRUCTION_LENGTH 5
#else
#pragma error "Current CPU is not supported yet"
#endif

#if defined(HAVE_QC_SUBMIT_BIO_NOACCT)
static blk_qc_t (*submit_bio_noacct_notrace)(struct bio *) =
	(blk_qc_t(*)(struct bio *))((unsigned long)(submit_bio_noacct) +
				    CALL_INSTRUCTION_LENGTH);
#elif defined(HAVE_VOID_SUBMIT_BIO_NOACCT)
static void (*submit_bio_noacct_notrace)(struct bio *) =
	(void (*)(struct bio *))((unsigned long)(submit_bio_noacct) +
				 CALL_INSTRUCTION_LENGTH);
#else
#error "Your kernel is too old for this module."
#endif

#if defined(HAVE_QC_SUBMIT_BIO_NOACCT)
static blk_qc_t notrace submit_bio_noacct_handler(struct bio *bio)
#else
static void notrace submit_bio_noacct_handler(struct bio *bio)
#endif
{
	if (!current->bio_list) {
		enum bdev_filter_altitudes altitude = bdev_filter_alt_unidentified;
		enum bdev_filter_result result;
		struct bio_list bio_list_on_stack[2] = { };
		struct bio *new_bio;

		do {
			bio_list_init(&bio_list_on_stack[0]);
			current->bio_list = bio_list_on_stack;
			barrier();

			result = bdev_filters_apply(bio, &altitude);

			current->bio_list = NULL;
			barrier();

			while ((new_bio = bio_list_pop(&bio_list_on_stack[0]))) {
				/*
				 * The result from submitting a bio from the
				 * filter itself does not need to be processed,
				 * even if this function has a return code.
				 */
				submit_bio_noacct_notrace(new_bio);
			}
		} while (result == bdev_filter_repeat);

		if (result == bdev_filter_skip) {
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

static struct klp_func funcs[] = {
	{
		.old_name = "submit_bio_noacct",
		.new_func = submit_bio_noacct_handler,
	},
	{ 0 }
};

static struct klp_object objs[] = {
	{
		/* name being NULL means vmlinux */
		.funcs = funcs,
	},
	{ 0 }
};

static struct klp_patch patch = {
	.mod = THIS_MODULE,
	.objs = objs,
};

static int __init lp_filter_init(void)
{
	return klp_enable_patch(&patch);
}

/*
 * For standalone only:
 * Before unload module all filters should be detached and livepatch are
 * disabled.
 *
 * echo 0 > /sys/kernel/livepatch/bdev_filter/enabled
 */
static void __exit lp_filter_done(void)
{
	struct bdev_extension *ext;

	while ((ext = list_first_entry_or_null(&bdev_extension_list,
					       struct bdev_extension, link))) {
		list_del(&ext->link);
		kfree(ext);
	}
}

module_init(lp_filter_init);
module_exit(lp_filter_done);

MODULE_DESCRIPTION("Block Device Filter kernel module");
MODULE_VERSION(VERSION_STR);
MODULE_AUTHOR("Veeam Software Group GmbH");
MODULE_LICENSE("GPL");
MODULE_INFO(livepatch, "Y");
/* Allow to be loaded on OpenSUSE/SLES */
MODULE_INFO(supported, "external");
