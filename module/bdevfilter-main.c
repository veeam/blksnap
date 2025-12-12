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
#include "log.h"

#include "bdevfilter-fops.h"
#include "bdevfilter-submit_bio.h"
#ifdef HAVE_BDEV_MARK_DEAD
#include "bdevfilter-bdev_mark_dead.h"
#elif defined(HAVE_DEL_GENDISK)
#include "bdevfilter-del_gendisk.h"
#endif

static void freeze_ref_release(struct percpu_ref *freeze_ref)
{
	struct blkfilter *flt = container_of(freeze_ref,
					     struct blkfilter, freeze_ref);
	wake_up_all(&flt->freeze_wq);
}

static int ioctl_attach(struct bdevfilter_attach __user *argp)
{
	char *devpath;
	struct bdevfilter_attach karg;
	struct bdevfilter_operations *fops;
	struct bdev_extension *ext_tmp, *ext_new;
	struct blkfilter *flt;
	bdev_holder_t *bdev_holder = NULL;
	struct block_device *bdev = NULL;
	unsigned int task_flags;
	int ret = 0;

	if (copy_from_user(&karg, argp, sizeof(karg)))
		return -EFAULT;

	devpath = strndup_user((const char __user *)karg.devpath, PATH_MAX);
	if (IS_ERR(devpath))
		return PTR_ERR(devpath);
	pr_debug("Attach filter '%s' to the block device '%s'\n", karg.name, devpath);
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
	bdev_mutex_lock(bdev);
	if (!device_alive(bdev)){
		pr_debug("Device is not alive\n");
		ret = -ENODEV;
		goto out_mutex_unlock;
	}

	task_flags = memalloc_noio_save();

	flt = fops->attach(bdev, devpath, (__u8 __user *)karg.opt, karg.optlen);
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
			pr_debug("Filter is already attached\n");
		} else {
			ret = -EBUSY;
			pr_debug("Device is busy\n");
		}
	} else {
		ext_new->flt = flt;
		list_add_tail(&ext_new->link, &bdev_extension_list);
		flt = NULL;
		ext_new = NULL;
		pr_debug("Filter attached\n");
	}
	spin_unlock(&bdev_extension_list_lock);

out_bdevfilter_put:
	bdevfilter_put(flt);
out_unfreeze:
	memalloc_noio_restore(task_flags);

out_mutex_unlock:
	bdev_mutex_unlock(bdev);
	kfree(ext_new);
out_blkdev_put:
	bdev_close(bdev_holder);
out_free_devpath:
	kfree(devpath);
	return ret;
}

static int __blkfilter_detach(dev_t dev_id, char *name, size_t name_length)
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

	if (ret) {
		pr_debug("Cannot detach filter from block device '%d:%d'\n",
			MAJOR(dev_id), MINOR(dev_id));
		if (ret == -ENOENT)
			pr_debug("Filter not found\n");
		else if (ret == -EINVAL)
			pr_debug("Invalid filters name\n");
	} else
		pr_debug("Filter detached\n");

	kfree(ext);
	bdevfilter_put(flt);
	return ret;
}

static int ioctl_detach(struct bdevfilter_name __user *argp)
{
	char *devpath;
	struct bdevfilter_name karg;
	bdev_holder_t *bdev_holder = NULL;
	struct block_device *bdev = NULL;
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

	bdev_mutex_lock(bdev);
	if (!device_alive(bdev))
		ret = -ENODEV;
	bdev_mutex_unlock(bdev);
	if (!ret)
		ret = __blkfilter_detach(bdev->bd_dev, karg.name, BDEVFILTER_NAME_LENGTH);

	bdev_close(bdev_holder);
out_free_devpath:
	kfree(devpath);
	return ret;
}

static int ioctl_ctl(struct bdevfilter_ctl __user *argp)
{
	char *devpath;
	struct bdevfilter_ctl karg;
	bdev_holder_t *bdev_holder = NULL;
	struct block_device *bdev = NULL;
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
		pr_debug("Filter for the block device '%d:%d' not found\n",
			MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev));
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

const char *log_thread_name = "bdevfilterlog";

static int ioctl_setlog(struct bdevfilter_setlog __user *argp)
{
	char *filepath = NULL;
	struct bdevfilter_setlog karg;

	if (copy_from_user(&karg, argp, sizeof(karg))) {
		pr_err("Unable to get log parameters: invalid user buffer\n");
		return -ENODATA;
	}

	/*
	 * logging can be disabled
	 * To do this, it is enough not to specify a logging file or set
	 * a negative logging level.
	 */
	if ((karg.level < 0) || !karg.filepath)
		return log_restart(-1, NULL, 0, log_thread_name);

	if (karg.filepath_size == 0) {
		pr_err("Invalid parameters. 'filepath_size' cannot be zero\n");
		return -EINVAL;
	}
	filepath = kzalloc(karg.filepath_size + 1, GFP_KERNEL);
	if (!filepath)
		return -ENOMEM;

	if (copy_from_user(filepath, (void *)karg.filepath,
			   karg.filepath_size)) {
		pr_err("Unable to get log filepath: invalid user buffer\n");

		kfree(filepath);
		return -ENODATA;
	}

	return log_restart(karg.level, filepath, karg.tz_minuteswest, log_thread_name);
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

static bool bdev_filters_apply(struct bio *bio)
{
	bool skip = false;
	struct bdev_extension *ext;
	struct blkfilter *flt = NULL;
	dev_t dev_id = bdevfilter_dev_id_by_bio(bio);

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

static long unlocked_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case BDEVFILTER_ATTACH:
		return ioctl_attach(argp);
	case BDEVFILTER_DETACH:
		return ioctl_detach(argp);
	case BDEVFILTER_CTL:
		return ioctl_ctl(argp);
	case BDEVFILTER_SETLOG:
		return ioctl_setlog(argp);
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
	if (ret) {
		pr_err("Failed to get address of the '%s'\n", "get_option");
		return ret;
	}
	kernel_base = (unsigned long)(get_option) - (unsigned long)addr;
	pr_debug("Function '%s' has been found\n", "get_option");

	ret = prepare_ftrace_free_filter(kernel_base);
	if (ret)
		return ret;
	return prepare_functions(kernel_base);
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
		bdevfilter_ftrace_free_filter(ops);
		return ret;
	}

	pr_debug("Ftrace filter for '%s' has been registered\n", name);
	return ret;
}

static void bdevfilter_unset(struct ftrace_ops *ops)
{
	unregister_ftrace_function(ops);
	bdevfilter_ftrace_free_filter(ops);
}

static int __init bdevfilter_init(void)
{
	int ret;

	log_init();
	/*
	ret = log_restart(7, "/var/log/veeam/bdevfilter.log", 0, log_tread_name);
	if (ret) {
		pr_err("Failed to prepare logging\n");
		return ret;
	}
	*/
	pr_debug("Loading\n");
	pr_debug("Version: %s\n", VERSION_STR);

	ret = prepare_fn();
	if (ret) {
		pr_err("Failed to prepare pointers to internal functions\n");
		return ret;
	}

	ret = set_submit_bio();
	if (ret)
		return ret;
	ret = set_functions();
	if (ret)
		goto out_unset_submit_bio_noacct;
	ret = misc_register(&bdevfilter_misc);
	if (ret) {
		pr_err("Failed to register control device (%d)\n", ret);
		goto out_unset_all;
	}
	return 0;

out_unset_all:
	unset_functions();
out_unset_submit_bio_noacct:
	unset_submit_bio();
	return ret;
}

static void __exit bdevfilter_done(void)
{
	misc_deregister(&bdevfilter_misc);
	unset_functions();
	unset_submit_bio();
	bdevfilter_detach_all(NULL);
	log_done();
}

module_init(bdevfilter_init);
module_exit(bdevfilter_done);

MODULE_DESCRIPTION("Block Device Filter kernel module");
MODULE_VERSION(VERSION_STR);
MODULE_AUTHOR("Veeam Software Group GmbH");
MODULE_LICENSE("GPL");
/* Allow to be loaded on OpenSUSE/SLES */
MODULE_INFO(supported, "external");
