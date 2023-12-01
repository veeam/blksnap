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

static inline struct bdevfilter_operations *bdevfilter_operations_get(
							 const char *name)
{
	struct bdevfilter_operations *fops;

	spin_lock(&bdevfilters_lock);
	fops = __bdevfilter_operations_find(name);
	if (fops && !try_module_get(fops->owner))
		fops = NULL;
	spin_unlock(&bdevfilters_lock);

	return fops;
}

static inline void bdevfilter_operations_put(
					const struct bdevfilter_operations *fops)
{
	if (likely(fops))
		module_put(fops->owner);
}

static inline struct block_device *bdev_by_fd(unsigned int fd)
{
	struct file *file;
	struct block_device *bdev = NULL;

	file = fget(fd);
	if (!file) {
		pr_err("Invalid file descriptor\n");
		return ERR_PTR(-EINVAL);
	}

	if (!S_ISBLK(file_inode(file)->i_mode)) {
		pr_err("Only block device file is allowed\n");
		bdev = ERR_PTR(-EINVAL);
	} else {
#if defined(HAVE_BLK_HOLDER_OPS)
#ifdef HAVE_BLK_OPEN_MODE
		bdev = blkdev_get_by_dev(file_inode(file)->i_rdev, BLK_OPEN_READ | BLK_OPEN_WRITE, NULL, NULL);
#else
		bdev = blkdev_get_by_dev(file_inode(file)->i_rdev, FMODE_READ | FMODE_WRITE, NULL, NULL);
#endif
#else
		bdev = blkdev_get_by_dev(file_inode(file)->i_rdev, FMODE_READ | FMODE_WRITE, NULL);
#endif
		if (IS_ERR(bdev))
			pr_err("Cannot open a block device\n");
	}

	fput(file);
	return bdev;
}

static int ioctl_attach(struct bdevfilter_name __user *argp)
{
	struct bdevfilter_name kargp;
	struct bdevfilter_operations *fops;
	struct bdev_extension *ext_tmp, *ext_new;
	struct blkfilter *flt;
	struct block_device *bdev;
	unsigned int task_flags;
	int ret = 0;

	if (copy_from_user(&kargp, argp, sizeof(kargp)))
		return -EFAULT;

	bdev = bdev_by_fd(kargp.bdev_fd);
	if (IS_ERR(bdev))
		return PTR_ERR(bdev);

	fops = bdevfilter_operations_get(kargp.name);
	if (!fops) {
		ret = -ENOENT;
		goto out_blkdev_put;
	}

	ext_new = kzalloc(sizeof(struct bdev_extension), GFP_NOIO);
	if (!ext_new) {
		ret = -ENOMEM;
		goto out_ops_put;
	}

	INIT_LIST_HEAD(&ext_new->link);
	ext_new->dev_id = bdev->bd_dev;

	mutex_lock(&bdev->bd_disk->open_mutex);
	if (!disk_live(bdev->bd_disk)) {
		ret = -ENODEV;
		goto out_mutex_unlock;
	}
	ret = freeze_bdev(bdev);
	if (ret)
		goto out_mutex_unlock;
	blk_mq_freeze_queue(bdev->bd_queue);
	task_flags = memalloc_noio_save();

	flt = fops->attach(bdev);
	if (IS_ERR(flt)) {
		ret = PTR_ERR(flt);
		goto out_unfreeze;
	}
	kref_init(&flt->kref);

	spin_lock(&bdev_extension_list_lock);
	ext_tmp = bdev_extension_find(bdev->bd_dev);
	if (ext_tmp) {
		if (ext_tmp->flt->fops == fops)
			ret = -EALREADY;
		else
			ret = -EBUSY;
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
	blk_mq_unfreeze_queue(bdev->bd_queue);
	thaw_bdev(bdev);
out_mutex_unlock:
	mutex_unlock(&bdev->bd_disk->open_mutex);
	kfree(ext_new);
out_ops_put:
	bdevfilter_operations_put(fops);
out_blkdev_put:
#if defined(HAVE_BLK_HOLDER_OPS)
	blkdev_put(bdev, NULL);
#else
	blkdev_put(bdev, FMODE_READ | FMODE_WRITE);
#endif
	return ret;
}

static inline void __blkfilter_detach(dev_t dev_id)
{
	struct bdev_extension *ext;
	struct blkfilter *flt = NULL;
	const struct bdevfilter_operations *fops = NULL;

	spin_lock(&bdev_extension_list_lock);
	ext = bdev_extension_find(dev_id);
	if (ext) {
		flt = ext->flt;
		fops = ext->flt->fops;
		list_del(&ext->link);
		kfree(ext);
	}
	spin_unlock(&bdev_extension_list_lock);

	bdevfilter_put(flt);
	bdevfilter_operations_put(fops);
}

void blkfilter_detach(struct block_device *bdev)
{
	blk_mq_freeze_queue(bdev->bd_queue);
	__blkfilter_detach(bdev->bd_dev);
	blk_mq_unfreeze_queue(bdev->bd_queue);
}

static inline int __blkfilter_detach2(dev_t dev_id, struct bdevfilter_name* kargp)
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
	bdevfilter_operations_put(fops);
	return ret;
}

static int ioctl_detach(struct bdevfilter_name __user *argp)
{
	struct bdevfilter_name kargp;
	struct block_device *bdev;
	int ret = 0;

	if (copy_from_user(&kargp, argp, sizeof(kargp)))
		return -EFAULT;

	bdev = bdev_by_fd(kargp.bdev_fd);
	if (IS_ERR(bdev))
		return PTR_ERR(bdev);

	mutex_lock(&bdev->bd_disk->open_mutex);
	if (!disk_live(bdev->bd_disk))
		ret = -ENODEV;
	else {
		blk_mq_freeze_queue(bdev->bd_queue);
		ret = __blkfilter_detach2(bdev->bd_dev, &kargp);
		blk_mq_unfreeze_queue(bdev->bd_queue);
	}
	mutex_unlock(&bdev->bd_disk->open_mutex);
#if defined(HAVE_BLK_HOLDER_OPS)
	blkdev_put(bdev, NULL);
#else
	blkdev_put(bdev, FMODE_READ | FMODE_WRITE);
#endif
	return ret;
}

static int ioctl_ctl(struct bdevfilter_ctl __user *argp)
{
	struct bdevfilter_ctl kargp;
	struct block_device *bdev;
	struct bdev_extension *ext;
	struct blkfilter *flt = NULL;
	int ret;

	if (copy_from_user(&kargp, argp, sizeof(kargp)))
		return -EFAULT;

	bdev = bdev_by_fd(kargp.bdev_fd);
	if (IS_ERR(bdev))
		return PTR_ERR(bdev);

	mutex_lock(&bdev->bd_disk->open_mutex);
	if (!disk_live(bdev->bd_disk)) {
		ret = -ENODEV;
		goto out_mutex_unlock;
	}

	//ret = blk_queue_enter(bdev_get_queue(bdev), 0);
	//if (ret)
	//	goto out_mutex_unlock;

	spin_lock(&bdev_extension_list_lock);
	ext = bdev_extension_find(bdev->bd_dev);
	if (ext && (strncmp(ext->flt->fops->name, kargp.name, BDEVFILTER_NAME_LENGTH) == 0))
		flt = bdevfilter_get(ext->flt);
	spin_unlock(&bdev_extension_list_lock);

	if (flt) {
		ret = flt->fops->ctl(flt, kargp.cmd, u64_to_user_ptr(kargp.opt),
			    &kargp.optlen);
		bdevfilter_put(flt);
	} else
		ret = -ENOENT;

	//blk_queue_exit(bdev_get_queue(bdev));
out_mutex_unlock:
	mutex_unlock(&bdev->bd_disk->open_mutex);
#if defined(HAVE_BLK_HOLDER_OPS)
	blkdev_put(bdev, NULL);
#else
	blkdev_put(bdev, FMODE_READ | FMODE_WRITE);
#endif
	return ret;
}

void bdevfilter_free(struct kref *kref)
{
	struct blkfilter *flt = container_of(kref, struct blkfilter, kref);

	flt->fops->detach(flt);
}

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

static inline bool bdev_filters_apply(struct bio *bio)
{
	bool result = true;
	struct bdev_extension *ext;
	struct blkfilter *flt = NULL;
#if defined(HAVE_BI_BDISK)
	struct hd_struct *part = disk_get_part(bio->bi_disk, bio->bi_partno);
	dev_t dev_id = part_devt(part);
#else
	dev_t dev_id = bio->bi_bdev->bd_dev;
#endif

	spin_lock(&bdev_extension_list_lock);
	ext = bdev_extension_find(dev_id);
	if (ext)
		flt = bdevfilter_get(ext->flt);
	spin_unlock(&bdev_extension_list_lock);
	if (flt) {
		result = flt->fops->submit_bio(bio, flt);
		bdevfilter_put(flt);
	}
#if defined(HAVE_BI_BDISK)
	disk_put_part(part);
#endif
	return result;
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
	if (!bdev_filters_apply(bio)) {
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

static const struct file_operations bdevfilter_ctl_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= unlocked_ioctl,
};

static struct miscdevice bdevfilter_ctl = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= BDEVFILTER,
	.fops		= &bdevfilter_ctl_fops,
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

	ret = misc_register(&bdevfilter_ctl);
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

	misc_deregister(&bdevfilter_ctl);

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
