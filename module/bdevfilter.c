// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/livepatch.h>
#include <linux/bio.h>
#ifdef HAVE_GENHD_H
#include <linux/genhd.h>
#endif
#include <linux/blkdev.h>
#include <linux/list.h>

#include "bdevfilter.h"
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

	INIT_LIST_HEAD(&ext_new->link);
	ext_new->dev_id = bdev->bd_dev;
#if defined(HAVE_BI_BDISK)
	ext_new->disk = bdev->bd_disk;
	ext_new->partno = bdev->bd_partno;
#endif
	spin_lock_init(&ext_new->bd_filters_lock);

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
 * bdev_filter_attach - Attach a filter to original block device.
 * @bdev:
 * 	block device
 * @name:
 *	Name of the block device filter.
 * @altitude:
 *	Number of the block device filter.
 * @flt:
 *	Pointer to the filter structure.
 *
 * The bdev_filter_detach() function allows to detach the filter from the block
 * device.
 *
 * Return:
 * 0 - OK
 * -EBUSY - a filter on this altitude already exists
 * -EINVAL - invalid altitude
 */
int bdev_filter_attach(struct block_device *bdev, const char *name,
		       const enum bdev_filter_altitudes altitude,
		       struct bdev_filter *flt)
{
	int ret = 0;
	struct bdev_extension *ext;

	pr_info("Attach block device filter '%s'", name);
	if ((altitude < 0) || (altitude >= bdev_filter_alt_end))
		return -EINVAL;

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


/*
 * Only for livepatch version
 * It is necessary for correct processing of the case when the block device
 * was removed from the system. Unlike the upstream version, we have no way
 * to handle device extension.
 */
int lp_bdev_filter_detach(const dev_t dev_id, const char *name,
			  const enum bdev_filter_altitudes altitude)
{
	struct bdev_extension *ext;
	struct bdev_filter *flt;

	pr_info("Detach block device filter '%s'", name);

	if ((altitude < 0) || (altitude >= bdev_filter_alt_end))
		return -EINVAL;

	spin_lock(&bdev_extension_list_lock);
	ext = bdev_extension_find(dev_id);
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
		name, MAJOR(dev_id), MINOR(dev_id));
	return 0;
}
EXPORT_SYMBOL(lp_bdev_filter_detach);

/**
 * bdev_filter_detach - Detach a filter from the block device.
 * @bdev:
 * 	block device.
 * @name:
 *	Name of the block device filter.
 * @altitude:
 *	Number of the block device filter.
 *
 * The filter should be added using the bdev_filter_attach() function.
 *
 * Return:
 * 0 - OK
 * -ENOENT - the filter was not found in the linked list
 * -EINVAL - invalid altitude
 */
int bdev_filter_detach(struct block_device *bdev, const char *name,
		       const enum bdev_filter_altitudes altitude)
{
	return lp_bdev_filter_detach(bdev->bd_dev, name, altitude);
}
EXPORT_SYMBOL(bdev_filter_detach);

/**
 * bdev_filter_get_by_altitude - Get filters context value.
 * @bdev:
 * 	Block device ID.
 * @altitude:
 * 	Number of the block device filter.
 *
 * Return pointer to &struct bdev_filter or NULL if the filter was not found.
 *
 * Necessary to lock list of filters by calling bdev_filter_read_lock().
 */
struct bdev_filter *bdev_filter_get_by_altitude(struct block_device *bdev,
				const enum bdev_filter_altitudes altitude)
{
	struct bdev_extension *ext;
	struct bdev_filter *flt = NULL;

	if ((altitude < 0) || (altitude >= bdev_filter_alt_end))
		return ERR_PTR(-EINVAL);

	spin_lock(&bdev_extension_list_lock);
	ext = bdev_extension_find(bdev->bd_dev);
	spin_unlock(&bdev_extension_list_lock);
	if (!ext)
		return NULL;

	spin_lock(&ext->bd_filters_lock);
	flt = ext->bd_filters[altitude];
	if (flt)
		bdev_filter_get(flt);
	spin_unlock(&ext->bd_filters_lock);

	return flt;
}
EXPORT_SYMBOL(bdev_filter_get_by_altitude);

static inline bool bdev_filters_apply(struct bio *bio)
{
	enum bdev_filter_altitudes altitude = 0;
	struct bdev_extension *ext;

	spin_lock(&bdev_extension_list_lock);
#if defined(HAVE_BI_BDISK)
	ext = bdev_extension_find_part(bio->bi_disk, bio->bi_partno);
#else
	ext = bdev_extension_find(bio->bi_bdev->bd_dev);
#endif
	spin_unlock(&bdev_extension_list_lock);
	if (!ext)
		return true;

	spin_lock(&ext->bd_filters_lock);
	while (altitude < bdev_filter_alt_end) {
		enum bdev_filter_result result;
		struct bdev_filter *flt;

		flt = ext->bd_filters[altitude];
		if (!flt) {
			altitude++;
			continue;
		}

		bdev_filter_get(flt);
		spin_unlock(&ext->bd_filters_lock);

		result = flt->fops->submit_bio_cb(bio, flt);
		bdev_filter_put(flt);

		if (result != bdev_filter_res_pass)
			return false;

		altitude++;

		spin_lock(&ext->bd_filters_lock);
	};
	spin_unlock(&ext->bd_filters_lock);

	return true;
}

#ifdef CONFIG_X86
#define CALL_INSTRUCTION_LENGTH 5
#else
#pragma error "Current CPU is not supported yet"
#endif

#if defined(HAVE_QC_SUBMIT_BIO_NOACCT)
blk_qc_t (*submit_bio_noacct_notrace)(struct bio *) =
	(blk_qc_t(*)(struct bio *))((unsigned long)(submit_bio_noacct) +
				    CALL_INSTRUCTION_LENGTH);
#elif defined(HAVE_VOID_SUBMIT_BIO_NOACCT)
void (*submit_bio_noacct_notrace)(struct bio *) =
	(void (*)(struct bio *))((unsigned long)(submit_bio_noacct) +
				 CALL_INSTRUCTION_LENGTH);
#else
#error "Your kernel is too old for this module."
#endif
EXPORT_SYMBOL(submit_bio_noacct_notrace);

#if defined(HAVE_QC_SUBMIT_BIO_NOACCT)
static blk_qc_t notrace submit_bio_noacct_handler(struct bio *bio)
#else
static void notrace submit_bio_noacct_handler(struct bio *bio)
#endif
{
	if (!current->bio_list) {
		if (!bdev_filters_apply(bio)) {
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

#ifdef CONFIG_LIVEPATCH
#pragma message("livepatch used")

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
MODULE_INFO(livepatch, "Y");

#elif defined(CONFIG_HAVE_DYNAMIC_FTRACE_WITH_ARGS)
#pragma message("ftrace filter used")

#ifdef HAVE_FTRACE_REGS
static void notrace ftrace_handler_submit_bio_noacct(unsigned long ip, unsigned long parent_ip,
			struct ftrace_ops *fops,
			struct ftrace_regs *fregs)
{
	ftrace_instruction_pointer_set(fregs, (unsigned long)submit_bio_noacct_handler);
}
#else
static void notrace ftrace_handler_submit_bio_noacct(unsigned long ip, unsigned long parent_ip,
			struct ftrace_ops *fops,
			struct pt_regs *regs)
{
	regs->ip = (unsigned long)submit_bio_noacct_handler;
}
#endif

unsigned char* funcname_submit_bio_noacct = "submit_bio_noacct";
static struct ftrace_ops ops_submit_bio_noacct = {
	.func = ftrace_handler_submit_bio_noacct,
	.flags = FTRACE_OPS_FL_DYNAMIC |
#ifndef CONFIG_HAVE_DYNAMIC_FTRACE_WITH_ARGS
		FTRACE_OPS_FL_SAVE_REGS |
#endif
		FTRACE_OPS_FL_IPMODIFY |
		FTRACE_OPS_FL_PERMANENT,
};

static int __init trace_filter_init(void)
{
	int ret = 0;

	ret = ftrace_set_filter(&ops_submit_bio_noacct, funcname_submit_bio_noacct, strlen(funcname_submit_bio_noacct), 0);
	if (ret) {
		pr_err("Failed to set ftrace filter for function '%s' (%d)\n", funcname_submit_bio_noacct, ret);
		goto err;
	}

	ret = register_ftrace_function(&ops_submit_bio_noacct);
	if (ret) {
		pr_err("Failed to register ftrace handler (%d)\n", ret);
		ftrace_set_filter(&ops_submit_bio_noacct, NULL, 0, 1);
		goto err;
	}

err:
	return ret;
}

static void __exit trace_filter_done(void)
{
	struct bdev_extension *ext;

	unregister_ftrace_function(&ops_submit_bio_noacct);

	spin_lock(&bdev_extension_list_lock);
	while ((ext = list_first_entry_or_null(&bdev_extension_list,
					       struct bdev_extension, link))) {
		list_del(&ext->link);
		kfree(ext);
	}
	spin_unlock(&bdev_extension_list_lock);
}

module_init(trace_filter_init);
module_exit(trace_filter_done);
#else
#error "The bdevfilter cannot be used for the current kernels configuration"
#endif

MODULE_DESCRIPTION("Block Device Filter kernel module");
MODULE_VERSION(VERSION_STR);
MODULE_AUTHOR("Veeam Software Group GmbH");
MODULE_LICENSE("GPL");
/* Allow to be loaded on OpenSUSE/SLES */
MODULE_INFO(supported, "external");
