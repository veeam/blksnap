// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Veeam Software Group GmbH */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/ftrace.h>
#include <linux/kprobes.h>
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

static void freeze_ref_release(struct percpu_ref *freeze_ref)
{
	struct blkfilter *flt = container_of(freeze_ref,
					     struct blkfilter, freeze_ref);
	wake_up_all(&flt->freeze_wq);
}

static int ioctl_attach(struct bdevfilter_name __user *argp)
{
	char *devpath;
	struct bdevfilter_name karg;
	struct bdevfilter_operations *fops;
	struct bdev_extension *ext_tmp, *ext_new;
	struct blkfilter *flt;
	bdev_holder_t *bdev_holder;
	struct block_device *bdev = NULL;
	unsigned int task_flags;
	int ret = 0;

	if (copy_from_user(&karg, argp, sizeof(karg)))
		return -EFAULT;

	devpath = strndup_user((const char __user *)karg.devpath, PATH_MAX);
	if (IS_ERR(devpath))
		return PTR_ERR(devpath);
	pr_debug("Attach '%s' to device '%s'\n", karg.name, devpath);
	ret = bdev_open(devpath, &bdev_holder, &bdev);
	if (ret) {
		pr_err("Failed to open a block device '%s'\n", devpath);
		goto out_free_devpath;
	}

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

	flt = fops->attach(bdev, devpath);
	if (IS_ERR(flt)) {
		pr_debug("Failed to attach device to filter '%s'\n", fops->name);
		ret = PTR_ERR(flt);
		goto out_unfreeze;
	}
	devpath = NULL;
	kref_init(&flt->kref);
	flt->fops = fops;

	flt->is_frozen = false;
	init_waitqueue_head(&flt->freeze_wq);
	ret = percpu_ref_init(&flt->freeze_ref, freeze_ref_release,
				PERCPU_REF_INIT_ATOMIC, GFP_KERNEL);
	if (ret)
		goto out_bdevfilter_put;

	spin_lock(&bdev_extension_list_lock);
	ext_tmp = bdev_extension_find(bdev->bd_dev);
	if (ext_tmp) {
		if (ext_tmp->flt->fops == fops) {
			ret = -EALREADY;
			pr_debug("Device is already attached\n");
		} else {
			ret = -EBUSY;
			pr_debug("Device is busy\n");
		}
	} else {
		ext_new->flt = flt;
		list_add_tail(&ext_new->link, &bdev_extension_list);
		flt = NULL;
		ext_new = NULL;
	}
	spin_unlock(&bdev_extension_list_lock);

out_bdevfilter_put:
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
	bdev_close(bdev_holder);
out_free_devpath:
	kfree(devpath);
	return ret;
}

static inline int __blkfilter_detach(dev_t dev_id, char *name, size_t name_length)
{
	int ret = 0;
	struct bdev_extension *ext = NULL;
	struct blkfilter *flt = NULL;
	const struct bdevfilter_operations *fops = NULL;

	spin_lock(&bdev_extension_list_lock);
	ext = bdev_extension_find(dev_id);
	if (!ext)
		ret = -ENOENT;
	else {
		if (name && strncmp(ext->flt->fops->name, name, name_length))
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
	bdev_holder_t *bdev_holder;
	struct block_device *bdev;
	int ret = 0;

	pr_debug("Block device filter detach\n");

	if (copy_from_user(&karg, argp, sizeof(karg)))
		return -EFAULT;
	devpath = strndup_user((const char __user *)karg.devpath, PATH_MAX);
	if (IS_ERR(devpath))
		return PTR_ERR(devpath);
	pr_debug("Detach '%s' from device '%s'\n", karg.name, devpath);

	ret = bdev_open(devpath, &bdev_holder, &bdev);
	if (ret) {
		pr_err("Failed to open a block device '%s'\n", devpath);
		goto out_free_devpath;
	}

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
		ret = __blkfilter_detach(bdev->bd_dev, karg.name, BDEVFILTER_NAME_LENGTH);
#ifdef HAVE_GENDISK_OPEN_MUTEX
	mutex_unlock(&bdev->bd_disk->open_mutex);
#else
	mutex_unlock(&bdev->bd_mutex);
#endif

	bdev_close(bdev_holder);
out_free_devpath:
	kfree(devpath);
	return ret;
}

static int ioctl_ctl(struct bdevfilter_ctl __user *argp)
{
	char *devpath;
	struct bdevfilter_ctl karg;
	bdev_holder_t *bdev_holder;
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

	ret = bdev_open(devpath, &bdev_holder, &bdev);
	if (ret) {
		pr_err("Failed to open a block device '%s'\n", devpath);
		goto out_free_devpath;
	}

	spin_lock(&bdev_extension_list_lock);
	ext = bdev_extension_find(bdev->bd_dev);
	if (ext && (strncmp(ext->flt->fops->name, karg.name, BDEVFILTER_NAME_LENGTH) == 0))
		flt = bdevfilter_get(ext->flt);
	spin_unlock(&bdev_extension_list_lock);

	if (!flt) {
		ret = -ENOENT;
		goto out_bdev_close;
	}

	ret = flt->fops->ctl(flt, karg.cmd, u64_to_user_ptr(karg.opt), &karg.optlen);
	bdevfilter_put(flt);

out_bdev_close:
	bdev_close(bdev_holder);
out_free_devpath:
	kfree(devpath);
	return ret;
}

void bdevfilter_free(struct kref *kref)
{
	struct blkfilter *flt = container_of(kref, struct blkfilter, kref);

	might_sleep();

	pr_debug("Detach filter '%s'\n", flt->fops->name);
	bdevfilter_freeze(flt);
	percpu_ref_exit(&flt->freeze_ref);
	flt->fops->detach(flt);
}
EXPORT_SYMBOL_GPL(bdevfilter_free);

void bdevfilter_detach_all(struct bdevfilter_operations *fops)
{
	struct bdev_extension *ext;

	spin_lock(&bdev_extension_list_lock);
	while ((ext = list_first_entry_or_null(&bdev_extension_list,
					       struct bdev_extension, link))) {
		list_del(&ext->link);
		if (fops && (ext->flt->fops != fops))
			continue;
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
		bdevfilter_enter(flt);
		skip = flt->fops->submit_bio(bio, flt);
		bdevfilter_exit(flt);
		bdevfilter_put(flt);
	}

	return skip;
}


/**
 * submit_bio_noacct_notrace() - Execute submit_bio_noacct() without handling.
 */
notrace __attribute__((optimize("no-optimize-sibling-calls")))
#if defined(HAVE_QC_SUBMIT_BIO_NOACCT)
blk_qc_t submit_bio_noacct_notrace(struct bio *bio)
#elif defined(HAVE_VOID_SUBMIT_BIO_NOACCT)
void submit_bio_noacct_notrace(struct bio *bio)
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

/*
 * ftrace for submit_bio_noacct()
 */
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
	return submit_bio_noacct(bio);
#elif defined(HAVE_VOID_SUBMIT_BIO_NOACCT)
	submit_bio_noacct(bio);
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
	if (current->bio_list || within_module(parent_ip, THIS_MODULE))
		return;

#if defined(HAVE_FTRACE_REGS_SET_INSTRUCTION_POINTER)
	ftrace_regs_set_instruction_pointer(fregs, (unsigned long)submit_bio_noacct_handler);
#elif defined(HAVE_FTRACE_REGS)
	ftrace_instruction_pointer_set(fregs, (unsigned long)submit_bio_noacct_handler);
#else
	instruction_pointer_set(regs, (unsigned long)submit_bio_noacct_handler);
#endif
}

static struct ftrace_ops ops_submit_bio_noacct = {
	.func = ftrace_handler_submit_bio_noacct,
	.flags = FTRACE_OPS_FL_DYNAMIC |
		FTRACE_OPS_FL_SAVE_REGS |
		FTRACE_OPS_FL_IPMODIFY |
		FTRACE_OPS_FL_PERMANENT,
};

#ifdef HAVE_BDEV_MARK_DEAD

/*
 * ftrace for bdev_mark_dead()
 */
static notrace __attribute__((optimize("no-optimize-sibling-calls")))
void bdev_mark_dead_handler(struct block_device *bdev, bool surprise)
{
	__blkfilter_detach(bdev->bd_dev, NULL, 0);
	bdev_mark_dead(bdev, surprise);
}

static notrace void ftrace_handler_bdev_mark_dead(
	unsigned long ip, unsigned long parent_ip, struct ftrace_ops *fops,
#ifdef HAVE_FTRACE_REGS
	struct ftrace_regs *fregs
#else
	struct pt_regs *regs
#endif
	)
{
	if (within_module(parent_ip, THIS_MODULE))
		return;

#if defined(HAVE_FTRACE_REGS_SET_INSTRUCTION_POINTER)
	ftrace_regs_set_instruction_pointer(fregs, (unsigned long)bdev_mark_dead_handler);
#elif defined(HAVE_FTRACE_REGS)
	ftrace_instruction_pointer_set(fregs, (unsigned long)bdev_mark_dead_handler);
#else
	instruction_pointer_set(regs, (unsigned long)bdev_mark_dead_handler);
#endif
}

static struct ftrace_ops ops_bdev_mark_dead = {
	.func = ftrace_handler_bdev_mark_dead,
	.flags = FTRACE_OPS_FL_DYNAMIC |
		FTRACE_OPS_FL_SAVE_REGS |
		FTRACE_OPS_FL_IPMODIFY |
		FTRACE_OPS_FL_PERMANENT,
};

#else

static inline void __blkfilter_detach_disk(struct gendisk *disk)
{
#ifdef HAVE_DISK_PART_ITER
	struct disk_part_iter piter;
	struct hd_struct *part;
	struct block_device *bdev;

	disk_part_iter_init(&piter, disk, DISK_PITER_INCL_EMPTY);
	while ((part = disk_part_iter_next(&piter))) {
		bdev = bdget_disk(disk, part->partno);
		if (!bdev)
			continue;
		__blkfilter_detach(bdev->bd_dev, NULL, 0);
		bdput(bdev);
	}
	disk_part_iter_exit(&piter);
	bdev = bdget_disk(disk, 0);
	if (!bdev)
		return;
	__blkfilter_detach(bdev->bd_dev, NULL, 0);
	bdput(bdev);
#else
	struct block_device *part;
	unsigned long idx;

	xa_for_each_start(&disk->part_tbl, idx, part, 1)
		__blkfilter_detach(part->bd_dev, NULL, 0);
	__blkfilter_detach(disk->part0->bd_dev, NULL, 0);
#endif
}

/*
 * ftrace for the del_gendisk()
 */
static notrace __attribute__((optimize("no-optimize-sibling-calls")))
void del_gendisk_handler(struct gendisk *disk)
{
	__blkfilter_detach_disk(disk);
	del_gendisk(disk);
}

static notrace void ftrace_handler_del_gendisk(
	unsigned long ip, unsigned long parent_ip, struct ftrace_ops *fops,
#ifdef HAVE_FTRACE_REGS
	struct ftrace_regs *fregs
#else
	struct pt_regs *regs
#endif
	)
{
	if (within_module(parent_ip, THIS_MODULE))
		return;

#if defined(HAVE_FTRACE_REGS_SET_INSTRUCTION_POINTER)
	ftrace_regs_set_instruction_pointer(fregs, (unsigned long)del_gendisk_handler);
#elif defined(HAVE_FTRACE_REGS)
	ftrace_instruction_pointer_set(fregs, (unsigned long)del_gendisk_handler);
#else
	instruction_pointer_set(regs, (unsigned long)del_gendisk_handler);
#endif
}

static struct ftrace_ops ops_del_gendisk = {
	.func = ftrace_handler_del_gendisk,
	.flags = FTRACE_OPS_FL_DYNAMIC |
		FTRACE_OPS_FL_SAVE_REGS |
		FTRACE_OPS_FL_IPMODIFY |
		FTRACE_OPS_FL_PERMANENT,
};

/*
 * ftrace for the bdev_disk_changed())
 */

#if defined(HAVE_BDEV_DISK_CHANGED_DISK)
static notrace __attribute__((optimize("no-optimize-sibling-calls")))
int bdev_disk_changed_handler(struct gendisk *disk, bool invalidate)
{
	__blkfilter_detach_disk(disk);
	return bdev_disk_changed(disk, invalidate);
}
#elif defined(HAVE_BDEV_DISK_CHANGED_BDEV)
static notrace __attribute__((optimize("no-optimize-sibling-calls")))
int bdev_disk_changed_handler(struct block_device *bdev, bool invalidate)
{
	__blkfilter_detach_disk(bdev->bd_disk);
	return bdev_disk_changed(bdev, invalidate);
}
#endif

static notrace void ftrace_handler_bdev_disk_changed(
	unsigned long ip, unsigned long parent_ip, struct ftrace_ops *fops,
#ifdef HAVE_FTRACE_REGS
	struct ftrace_regs *fregs
#else
	struct pt_regs *regs
#endif
	)
{
	if (within_module(parent_ip, THIS_MODULE))
		return;

#if defined(HAVE_FTRACE_REGS_SET_INSTRUCTION_POINTER)
	ftrace_regs_set_instruction_pointer(fregs, (unsigned long)bdev_disk_changed_handler);
#elif defined(HAVE_FTRACE_REGS)
	ftrace_instruction_pointer_set(fregs, (unsigned long)bdev_disk_changed_handler);
#else
	instruction_pointer_set(regs, (unsigned long)bdev_disk_changed_handler);
#endif
}

static struct ftrace_ops ops_bdev_disk_changed = {
	.func = ftrace_handler_bdev_disk_changed,
	.flags = FTRACE_OPS_FL_DYNAMIC |
		FTRACE_OPS_FL_SAVE_REGS |
		FTRACE_OPS_FL_IPMODIFY |
		FTRACE_OPS_FL_PERMANENT,
};

#endif

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

#ifdef HAVE_NOT_FTRACE_FREE_FILTER
static unsigned long addr_ftrace_free_filter;
#endif

static int get_symbol(const char *name, void **paddr)
{
	int ret;
	struct kprobe kp = {0};

	kp.symbol_name = name;
	ret = register_kprobe(&kp);
	if (ret) {
		pr_err("Failed to get address of the '%s'\n", name);
		return ret;
	}

	*paddr = kp.addr;
	unregister_kprobe(&kp);

	return 0;
}

static int prepare_fn(void )
{
	int ret;

	unsigned long kernel_base;
	void *addr;

	ret = get_symbol("get_option", &addr);
	if (ret)
		return ret;
	kernel_base = (unsigned long)(get_option) - (unsigned long)addr;

#ifdef HAVE_NOT_FTRACE_FREE_FILTER
	ret = get_symbol("ftrace_free_filter", &addr);
	if (ret)
		return ret;
	addr_ftrace_free_filter = kernel_base + (unsigned long)addr;
#endif
	return 0;
}

static int bdevfilter_set(struct ftrace_ops *ops, unsigned char *name)
{
	int ret;

	ret = ftrace_set_filter(ops, name, strlen(name), 0);
	if (ret) {
		pr_err("Failed to set ftrace handler for function '%s'\n", name);
		return ret;
	}

	ret = register_ftrace_function(ops);
	if (ret) {
		pr_err("Failed to register ftrace handler (%d)\n", ret);
#ifdef HAVE_NOT_FTRACE_FREE_FILTER
		((void (*)(struct ftrace_ops *ops))addr_ftrace_free_filter)(ops);
#else
		ftrace_free_filter(ops);
#endif
		return ret;
	}

	pr_debug("Ftrace filter for '%s' has been registered\n", name);
	return ret;
}

static void bdevfilter_unset(struct ftrace_ops *ops)
{
	unregister_ftrace_function(ops);
#ifdef HAVE_NOT_FTRACE_FREE_FILTER
	((void (*)(struct ftrace_ops *ops))addr_ftrace_free_filter)(ops);
#else
	ftrace_free_filter(ops);
#endif
}

static int __init bdevfilter_init(void)
{
	int ret;

	ret = prepare_fn();
	if (ret) {
		pr_err("Failed to prepare pointers to internal functions\n");
		return ret;
	}

	ret = bdevfilter_set(&ops_submit_bio_noacct, "submit_bio_noacct");
	if (ret)
		return ret;

#ifdef HAVE_BDEV_MARK_DEAD
	ret = bdevfilter_set(&ops_bdev_mark_dead, "bdev_mark_dead");
	if (ret)
		goto out_unset_submit_bio_noacct;
#else
	ret = bdevfilter_set(&ops_del_gendisk, "del_gendisk");
	if (ret)
		goto out_unset_submit_bio_noacct;

	ret = bdevfilter_set(&ops_bdev_disk_changed, "bdev_disk_changed");
	if (ret) {
		goto out_unset_del_gendisk;
	}
#endif

	ret = misc_register(&bdevfilter_misc);
	if (ret) {
		pr_err("Failed to register control device (%d)\n", ret);
		goto out_unset_all;
	}

	return 0;

out_unset_all:
#ifdef HAVE_BDEV_MARK_DEAD
	bdevfilter_unset(&ops_bdev_mark_dead);
#else
	bdevfilter_unset(&ops_bdev_disk_changed);
out_unset_del_gendisk:
	bdevfilter_unset(&ops_del_gendisk);
#endif
out_unset_submit_bio_noacct:
	bdevfilter_unset(&ops_submit_bio_noacct);

	return ret;
}

static void __exit bdevfilter_done(void)
{
	misc_deregister(&bdevfilter_misc);
#ifdef HAVE_BDEV_MARK_DEAD
	bdevfilter_unset(&ops_bdev_mark_dead);
#else
	bdevfilter_unset(&ops_bdev_disk_changed);
	bdevfilter_unset(&ops_del_gendisk);
#endif
	bdevfilter_unset(&ops_submit_bio_noacct);

	bdevfilter_detach_all(NULL);
}

module_init(bdevfilter_init);
module_exit(bdevfilter_done);

MODULE_DESCRIPTION("Block Device Filter kernel module");
MODULE_VERSION(VERSION_STR);
MODULE_AUTHOR("Veeam Software Group GmbH");
MODULE_LICENSE("GPL");
/* Allow to be loaded on OpenSUSE/SLES */
MODULE_INFO(supported, "external");
