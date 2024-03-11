// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Veeam Software Group GmbH */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/ftrace.h>
#include <linux/sched/task.h>
#include <linux/file.h>
#include <linux/bio.h>
#ifdef HAVE_GENHD_H
#include <linux/genhd.h>
#endif
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include "bdevfilter.h"
#include "bdevfilter-internal.h"
#include "compat.h"
#include "version.h"


struct bdev_extension {
	struct list_head link;
	dev_t dev_id;
	struct blkfilter *flt;
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


static LIST_HEAD(bdevfilters);
static DEFINE_SPINLOCK(bdevfilters_lock);

static inline struct bdevfilter_operations *__bdevfilter_operations_find(
							const char *name)
{
	struct bdevfilter_operations *fops;

	list_for_each_entry(fops, &bdevfilters, link)
		if (strncmp(fops->name, name, BDEVFILTER_NAME_LENGTH) == 0)
			return fops;

	return NULL;
}

static inline struct bdevfilter_operations *bdevfilter_operations_find(
							 const char *name)
{
	struct bdevfilter_operations *fops;

	spin_lock(&bdevfilters_lock);
	fops = __bdevfilter_operations_find(name);
	spin_unlock(&bdevfilters_lock);

	return fops;
}

static int ioctl_attach(struct bdevfilter_name __user *argp)
{
	char *devpath;
	struct bdevfilter_name karg;
	struct bdevfilter_operations *fops;
	struct bdev_extension *ext_tmp, *ext_new;
	struct blkfilter *flt;
#if defined(HAVE_BDEV_HANDLE)
	struct bdev_handle *bdev_handle;
#endif
	struct block_device *bdev;
	unsigned int task_flags;
	int ret = 0;

	if (copy_from_user(&karg, argp, sizeof(karg)))
		return -EFAULT;

	devpath = strndup_user((const char __user *)karg.devpath, PATH_MAX);
	if (IS_ERR(devpath))
		return PTR_ERR(devpath);
	pr_debug("Attach '%s' to device '%s'\n", karg.name, devpath);
#if defined(HAVE_BDEV_HANDLE)
	bdev_handle = bdev_open_by_path(devpath,
					BLK_OPEN_READ | BLK_OPEN_WRITE,
					NULL, NULL);
	if (IS_ERR(bdev_handle)) {
		pr_err("Failed to open a block device '%s'\n", devpath);
		ret = PTR_ERR(bdev_handle);
		goto out_free_devpath;
	}
	bdev = bdev_handle->bdev;
#else
	bdev = blkdev_get_by_path(devpath,
#if defined(HAVE_BLK_HOLDER_OPS)
				BLK_OPEN_READ | BLK_OPEN_WRITE, NULL, NULL
#else
				FMODE_READ | FMODE_WRITE, NULL
#endif
				);
	if (IS_ERR(bdev)) {
		pr_err("Failed to open a block device '%s'\n", devpath);
		ret = PTR_ERR(bdev);
		goto out_free_devpath;
	}
#endif

	fops = bdevfilter_operations_find(karg.name);
	if (!fops) {
		pr_debug("Filter '%s' is not registered\n", karg.name);
		ret = -ENOENT;
		goto out_blkdev_put;
	}

	ext_new = kzalloc(sizeof(struct bdev_extension), GFP_NOIO);
	if (!ext_new) {
		ret = -ENOMEM;
		goto out_blkdev_put;
	}

	INIT_LIST_HEAD(&ext_new->link);
	ext_new->dev_id = bdev->bd_dev;
#ifdef HAVE_GENDISK_OPEN_MUTEX
	mutex_lock(&bdev->bd_disk->open_mutex);
#else
	mutex_lock(&bdev->bd_mutex);
#endif
#ifdef HAVE_DISK_LIVE
	if (!disk_live(bdev->bd_disk))
#else
	if (inode_unhashed(bdev->bd_inode))
#endif
	{
		pr_debug("Device is not alive\n");
		ret = -ENODEV;
		goto out_mutex_unlock;
	}

	task_flags = memalloc_noio_save();

	flt = fops->attach(bdev);
	if (IS_ERR(flt)) {
		pr_debug("Failed to attach device to filter '%s'\n", fops->name);
		ret = PTR_ERR(flt);
		goto out_unfreeze;
	}
	kref_init(&flt->kref);

	spin_lock(&bdev_extension_list_lock);
	ext_tmp = bdev_extension_find(bdev->bd_dev);
	if (ext_tmp) {
		if (ext_tmp->flt->fops == fops) {
			ret = -EALREADY;
			pr_debug("Device is already attached\n");
		}
		else {
			ret = -EBUSY;
			pr_debug("Device is busy\n");
		}
	} else {
		flt->fops = fops;
		ext_new->flt = flt;
		list_add_tail(&ext_new->link, &bdev_extension_list);
		fops = NULL;
		flt = NULL;
		ext_new = NULL;
	}
	spin_unlock(&bdev_extension_list_lock);

	bdevfilter_put(flt);
out_unfreeze:
	memalloc_noio_restore(task_flags);

out_mutex_unlock:
#ifdef HAVE_GENDISK_OPEN_MUTEX
	mutex_unlock(&bdev->bd_disk->open_mutex);
#else
	mutex_unlock(&bdev->bd_mutex);
#endif
	kfree(ext_new);
out_blkdev_put:
#if defined(HAVE_BDEV_HANDLE)
	bdev_release(bdev_handle);
#else

	blkdev_put(bdev,
#if defined(HAVE_BLK_HOLDER_OPS)
		NULL
#else
		FMODE_READ | FMODE_WRITE
#endif
		);
#endif
out_free_devpath:
	kfree(devpath);
	return ret;
}

static inline int __blkfilter_detach(dev_t dev_id, struct bdevfilter_name* kargp)
{
	int ret = 0;
	struct bdev_extension *ext;
	struct blkfilter *flt = NULL;
	const struct bdevfilter_operations *fops = NULL;

	spin_lock(&bdev_extension_list_lock);
	ext = bdev_extension_find(dev_id);
	if (!ext)
		ret = -ENOENT;
	else {
		if (strncmp(ext->flt->fops->name, kargp->name, BDEVFILTER_NAME_LENGTH))
			ret = -EINVAL;
		else {
			flt = ext->flt;
			fops = ext->flt->fops;
			list_del(&ext->link);
		}
	}
	spin_unlock(&bdev_extension_list_lock);

	kfree(ext);
	bdevfilter_put(flt);
	return ret;
}

static int ioctl_detach(struct bdevfilter_name __user *argp)
{
	char *devpath;
	struct bdevfilter_name karg;
#if defined(HAVE_BDEV_HANDLE)
	struct bdev_handle *bdev_handle;
#endif
	struct block_device *bdev;
	int ret = 0;

	pr_debug("Block device filter detach\n");

	if (copy_from_user(&karg, argp, sizeof(karg)))
		return -EFAULT;
	devpath = strndup_user((const char __user *)karg.devpath, PATH_MAX);
	if (IS_ERR(devpath))
		return PTR_ERR(devpath);
	pr_debug("Detach '%s' from device '%s'\n", karg.name, devpath);
#if defined(HAVE_BDEV_HANDLE)
	bdev_handle = bdev_open_by_path(devpath,
					BLK_OPEN_READ | BLK_OPEN_WRITE,
					NULL, NULL);
	if (IS_ERR(bdev_handle)) {
		pr_err("Failed to open a block device '%s'\n", devpath);
		ret = PTR_ERR(bdev_handle);
		goto out_free_devpath;
	}
	bdev = bdev_handle->bdev;
#else
	bdev = blkdev_get_by_path(devpath,
#if defined(HAVE_BLK_HOLDER_OPS)
				BLK_OPEN_READ | BLK_OPEN_WRITE, NULL, NULL
#else
				FMODE_READ | FMODE_WRITE, NULL
#endif
				);
	if (IS_ERR(bdev)) {
		pr_err("Failed to open a block device '%s'\n", devpath);
		ret = PTR_ERR(bdev);
		goto out_free_devpath;
	}
#endif

#ifdef HAVE_GENDISK_OPEN_MUTEX
	mutex_lock(&bdev->bd_disk->open_mutex);
#else
	mutex_lock(&bdev->bd_mutex);
#endif
#ifdef HAVE_DISK_LIVE
	if (!disk_live(bdev->bd_disk))
#else
	if (inode_unhashed(bdev->bd_inode))
#endif
		ret = -ENODEV;
	else
		ret = __blkfilter_detach(bdev->bd_dev, &karg);
#ifdef HAVE_GENDISK_OPEN_MUTEX
	mutex_unlock(&bdev->bd_disk->open_mutex);
#else
	mutex_unlock(&bdev->bd_mutex);
#endif
#if defined(HAVE_BDEV_HANDLE)
	bdev_release(bdev_handle);
#else
	blkdev_put(bdev,
#if defined(HAVE_BLK_HOLDER_OPS)
		NULL
#else
		FMODE_READ | FMODE_WRITE
#endif
		);
#endif
out_free_devpath:
	kfree(devpath);
	return ret;
}

static int ioctl_ctl(struct bdevfilter_ctl __user *argp)
{
	char *devpath;
	struct bdevfilter_ctl karg;
#if defined(HAVE_BDEV_HANDLE)
	struct bdev_handle *bdev_handle;
#endif
	struct block_device *bdev;
	struct bdev_extension *ext;
	struct blkfilter *flt = NULL;
	int ret = 0;

	pr_debug("Block device filter ioctl\n");

	if (copy_from_user(&karg, argp, sizeof(karg)))
		return -EFAULT;
	devpath = strndup_user((const char __user *)karg.devpath, PATH_MAX);
	if (IS_ERR(devpath))
		return PTR_ERR(devpath);
	pr_debug("Control '%s' to device '%s'\n", karg.name, devpath);
#if defined(HAVE_BDEV_HANDLE)
	bdev_handle = bdev_open_by_path(devpath,
					BLK_OPEN_READ | BLK_OPEN_WRITE,
					NULL, NULL);
	if (IS_ERR(bdev_handle)) {
		pr_err("Failed to open a block device '%s'\n", devpath);
		ret = PTR_ERR(bdev_handle);
		goto out_free_devpath;
	}
	bdev = bdev_handle->bdev;
#else
	bdev = blkdev_get_by_path(devpath,
#if defined(HAVE_BLK_HOLDER_OPS)
				BLK_OPEN_READ | BLK_OPEN_WRITE, NULL, NULL
#else
				FMODE_READ | FMODE_WRITE, NULL
#endif
				);
	if (IS_ERR(bdev)) {
		pr_err("Failed to open a block device '%s'\n", devpath);
		ret = PTR_ERR(bdev);
		goto out_free_devpath;
	}
#endif

#ifdef HAVE_GENDISK_OPEN_MUTEX
	mutex_lock(&bdev->bd_disk->open_mutex);
#else
	mutex_lock(&bdev->bd_mutex);
#endif
#ifdef HAVE_DISK_LIVE
	if (!disk_live(bdev->bd_disk))
#else
	if (inode_unhashed(bdev->bd_inode))
#endif
	{
		ret = -ENODEV;
		goto out_mutex_unlock;
	}

	//ret = blk_queue_enter(bdev_get_queue(bdev), 0);
	//if (ret)
	//	goto out_mutex_unlock;

	spin_lock(&bdev_extension_list_lock);
	ext = bdev_extension_find(bdev->bd_dev);
	if (ext && (strncmp(ext->flt->fops->name, karg.name, BDEVFILTER_NAME_LENGTH) == 0))
		flt = bdevfilter_get(ext->flt);
	spin_unlock(&bdev_extension_list_lock);

	if (flt) {
		ret = flt->fops->ctl(flt, karg.cmd, u64_to_user_ptr(karg.opt),
			    &karg.optlen);
		bdevfilter_put(flt);
	} else
		ret = -ENOENT;

	//blk_queue_exit(bdev_get_queue(bdev));
out_mutex_unlock:
#ifdef HAVE_GENDISK_OPEN_MUTEX
	mutex_unlock(&bdev->bd_disk->open_mutex);
#else
	mutex_unlock(&bdev->bd_mutex);
#endif
#if defined(HAVE_BDEV_HANDLE)
	bdev_release(bdev_handle);
#else
	blkdev_put(bdev,
#if defined(HAVE_BLK_HOLDER_OPS)
		NULL
#else
		FMODE_READ | FMODE_WRITE
#endif
		);
#endif
out_free_devpath:
	kfree(devpath);
	return ret;
}

void bdevfilter_free(struct kref *kref)
{
	struct blkfilter *flt = container_of(kref, struct blkfilter, kref);

	pr_debug("Detach filter '%s' registered\n",
			flt->fops->name);
	flt->fops->detach(flt);
}

void bdevfilter_detach_all(struct bdevfilter_operations *fops)
{
	struct bdev_extension *ext;

	spin_lock(&bdev_extension_list_lock);
	while ((ext = list_first_entry_or_null(&bdev_extension_list,
					       struct bdev_extension, link))) {
		list_del(&ext->link);
		spin_unlock(&bdev_extension_list_lock);

		bdevfilter_put(ext->flt);
		kfree(ext);

		spin_lock(&bdev_extension_list_lock);
	}
	spin_unlock(&bdev_extension_list_lock);
}
EXPORT_SYMBOL_GPL(bdevfilter_detach_all);

int bdevfilter_register(struct bdevfilter_operations *fops)
{
	struct bdevfilter_operations *found;
	int ret = 0;

	spin_lock(&bdevfilters_lock);
	found = __bdevfilter_operations_find(fops->name);
	if (found)
		ret = -EBUSY;
	else
		list_add_tail(&fops->link, &bdevfilters);
	spin_unlock(&bdevfilters_lock);

	if (ret)
		pr_warn("Failed to register block device filter %s\n",
			fops->name);
	else
		pr_debug("The block device filter '%s' registered\n",
			fops->name);

	return ret;
}
EXPORT_SYMBOL_GPL(bdevfilter_register);


void bdevfilter_unregister(struct bdevfilter_operations *fops)
{
	spin_lock(&bdevfilters_lock);
	list_del(&fops->link);
	spin_unlock(&bdevfilters_lock);
}
EXPORT_SYMBOL_GPL(bdevfilter_unregister);

#if defined(HAVE_BI_BDISK)
static inline struct hd_struct *bdevfilter_disk_get_part(struct gendisk *disk, int partno)
{
	struct disk_part_tbl *ptbl = rcu_dereference(disk->part_tbl);

	if (unlikely(partno < 0 || partno >= ptbl->len))
		return NULL;
	return rcu_dereference(ptbl->part[partno]);
}
static inline dev_t bdevfilter_disk_get_dev(struct gendisk *disk, int partno)
{
	dev_t dev_id = 0;
	struct hd_struct *part;

	rcu_read_lock();
	part = bdevfilter_disk_get_part(disk, partno);
	if (part)
		dev_id = part_devt(part);
	rcu_read_unlock();

	return dev_id;
}
#endif

static inline bool bdev_filters_apply(struct bio *bio)
{
	bool skip = false;
	struct bdev_extension *ext;
	struct blkfilter *flt = NULL;
#if defined(HAVE_BI_BDISK)
	dev_t dev_id = bdevfilter_disk_get_dev(bio->bi_disk, bio->bi_partno);
#else
	dev_t dev_id = bio->bi_bdev->bd_dev;
#endif

	spin_lock(&bdev_extension_list_lock);
	ext = bdev_extension_find(dev_id);
	if (ext)
		flt = bdevfilter_get(ext->flt);
	spin_unlock(&bdev_extension_list_lock);
	if (flt) {
		skip = flt->fops->submit_bio(bio, flt);
		bdevfilter_put(flt);
	}

	return skip;
}


/**
 * submit_bio_noacct_notrace() - Execute submit_bio_noacct() without handling.
 */
#if defined(HAVE_QC_SUBMIT_BIO_NOACCT)
notrace blk_qc_t submit_bio_noacct_notrace(struct bio *bio)
#elif defined(HAVE_VOID_SUBMIT_BIO_NOACCT)
notrace void submit_bio_noacct_notrace(struct bio *bio)
#else
#error "Your kernel is too old for this module."
#endif
{
#if defined(HAVE_QC_SUBMIT_BIO_NOACCT)
	return submit_bio_noacct(bio);
#elif defined(HAVE_VOID_SUBMIT_BIO_NOACCT)
	submit_bio_noacct(bio);
#endif
}
EXPORT_SYMBOL_GPL(submit_bio_noacct_notrace);

static notrace __attribute__((optimize("no-optimize-sibling-calls")))
#if defined(HAVE_QC_SUBMIT_BIO_NOACCT)
blk_qc_t submit_bio_noacct_handler(struct bio *bio)
#elif defined(HAVE_VOID_SUBMIT_BIO_NOACCT)
void submit_bio_noacct_handler(struct bio *bio)
#endif
{
	if (bdev_filters_apply(bio)) {
#if defined(HAVE_QC_SUBMIT_BIO_NOACCT)
		return BLK_QC_T_NONE;
#elif defined(HAVE_VOID_SUBMIT_BIO_NOACCT)
		return;
#endif
	}

#if defined(HAVE_QC_SUBMIT_BIO_NOACCT)
	return submit_bio_noacct_notrace(bio);
#elif defined(HAVE_VOID_SUBMIT_BIO_NOACCT)
	submit_bio_noacct_notrace(bio);
#endif
}

static notrace void ftrace_handler_submit_bio_noacct(
	unsigned long ip, unsigned long parent_ip, struct ftrace_ops *fops,
#ifdef HAVE_FTRACE_REGS
	struct ftrace_regs *fregs
#else
	struct pt_regs *regs
#endif
	)
{
	if (!current->bio_list && !within_module(parent_ip, THIS_MODULE)) {
#if defined(HAVE_FTRACE_REGS_SET_INSTRUCTION_POINTER)
		ftrace_regs_set_instruction_pointer(fregs, (unsigned long)submit_bio_noacct_handler);
#elif defined(HAVE_FTRACE_REGS)
		ftrace_instruction_pointer_set(fregs, (unsigned long)submit_bio_noacct_handler);
#else
		instruction_pointer_set(regs, (unsigned long)submit_bio_noacct_handler);
#endif
	}
}

static struct ftrace_ops ops_submit_bio_noacct = {
	.func = ftrace_handler_submit_bio_noacct,
	.flags = FTRACE_OPS_FL_DYNAMIC |
		FTRACE_OPS_FL_SAVE_REGS |
		FTRACE_OPS_FL_IPMODIFY |
		FTRACE_OPS_FL_PERMANENT,
};

static long unlocked_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	void *argp = (void __user *)arg;

	switch (cmd) {
	case BDEVFILTER_ATTACH:
		return ioctl_attach(argp);
	case BDEVFILTER_DETACH:
		return ioctl_detach(argp);
	case BDEVFILTER_CTL:
		return ioctl_ctl(argp);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations bdevfilter_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= unlocked_ioctl,
};

static struct miscdevice bdevfilter_misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= BDEVFILTER,
	.fops		= &bdevfilter_fops,
};

static int __init bdevfilter_init(void)
{
	int ret;

	ret = ftrace_set_filter(&ops_submit_bio_noacct, "submit_bio_noacct", strlen("submit_bio_noacct"), 0);
	if (ret) {
		pr_err("Failed to set ftrace handler for function 'submit_bio_noacct'\n");
		return ret;
	}

	ret = register_ftrace_function(&ops_submit_bio_noacct);
	if (ret) {
		pr_err("Failed to register ftrace handler (%d)\n", ret);
		ftrace_set_filter(&ops_submit_bio_noacct, NULL, 0, 1);
		return ret;
	}

	ret = misc_register(&bdevfilter_misc);
	if (ret) {
		pr_err("Failed to register control device (%d)\n", ret);
		ftrace_set_filter(&ops_submit_bio_noacct, NULL, 0, 1);
		unregister_ftrace_function(&ops_submit_bio_noacct);
		return ret;
	}

	pr_debug("Ftrace filter for 'submit_bio_noacct' has been registered\n");
	return 0;
}

static void __exit bdevfilter_done(void)
{
	struct bdev_extension *ext;

	misc_deregister(&bdevfilter_misc);

	unregister_ftrace_function(&ops_submit_bio_noacct);

	pr_debug("Ftrace filter for 'submit_bio_noacct' has been unregistered\n");

	spin_lock(&bdev_extension_list_lock);
	while ((ext = list_first_entry_or_null(&bdev_extension_list,
					       struct bdev_extension, link))) {
		list_del(&ext->link);
		kfree(ext);
	}
	spin_unlock(&bdev_extension_list_lock);
}

module_init(bdevfilter_init);
module_exit(bdevfilter_done);

MODULE_DESCRIPTION("Block Device Filter kernel module");
MODULE_VERSION(VERSION_STR);
MODULE_AUTHOR("Veeam Software Group GmbH");
MODULE_LICENSE("GPL");
/* Allow to be loaded on OpenSUSE/SLES */
MODULE_INFO(supported, "external");
