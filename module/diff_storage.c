// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Veeam Software Group GmbH */
#define pr_fmt(fmt) KBUILD_MODNAME "-diff-storage: " fmt

#include <linux/slab.h>
#include <linux/sched/mm.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/file.h>
#include <linux/blkdev.h>
#include <linux/build_bug.h>
#ifdef BLKSNAP_STANDALONE
#include "veeamblksnap.h"
#include "compat.h"
#else
#include <uapi/linux/blksnap.h>
#endif
#include "chunk.h"
#include "diff_buffer.h"
#include "diff_storage.h"
#include "params.h"

static void diff_storage_reallocate_work(struct work_struct *work)
{
	int ret;
	sector_t req_sect;
	struct diff_storage *diff_storage = container_of(
		work, struct diff_storage, reallocate_work);
	bool complete = false;

	do {
		spin_lock(&diff_storage->lock);
		req_sect = diff_storage->requested;
		spin_unlock(&diff_storage->lock);

		ret = vfs_fallocate(diff_storage->file, 0, 0,
				    (loff_t)(req_sect << SECTOR_SHIFT));
		if (ret) {
			pr_err("Failed to fallocate difference storage file\n");
			break;
		}

		spin_lock(&diff_storage->lock);
		diff_storage->capacity = req_sect;
		complete = (diff_storage->capacity >= diff_storage->requested);
		if (complete)
			atomic_set(&diff_storage->low_space_flag, 0);
		spin_unlock(&diff_storage->lock);

		pr_debug("Diff storage reallocate. Capacity: %llu sectors\n",
			 req_sect);
	} while (!complete);
}

static bool diff_storage_calculate_requested(struct diff_storage *diff_storage)
{
	bool ret = false;

	spin_lock(&diff_storage->lock);
	if (diff_storage->capacity < diff_storage->limit) {
		diff_storage->requested += min(get_diff_storage_minimum(),
				diff_storage->limit - diff_storage->capacity);
		ret = true;
	}
	pr_debug("The size of the difference storage was %llu MiB\n",
		 diff_storage->capacity >> (20 - SECTOR_SHIFT));
	pr_debug("The limit is %llu MiB\n",
		 diff_storage->limit >> (20 - SECTOR_SHIFT));
	spin_unlock(&diff_storage->lock);

	return ret;
}

static inline bool is_halffull(const sector_t sectors_left)
{
	return sectors_left <= (get_diff_storage_minimum() / 2);
}

static inline void check_halffull(struct diff_storage *diff_storage,
				  const sector_t sectors_left)
{
	if (is_halffull(sectors_left) &&
	    (atomic_inc_return(&diff_storage->low_space_flag) == 1)) {

#if defined(CONFIG_BLKSNAP_DIFF_BLKDEV)
		if (diff_storage->bdev) {
			pr_warn("Reallocating is allowed only for a regular file\n");
			return;
		}
#endif
		if (!diff_storage_calculate_requested(diff_storage)) {
			pr_info("The limit size of the difference storage has been reached\n");
			return;
		}

		pr_debug("Diff storage low free space.\n");
		queue_work(system_wq, &diff_storage->reallocate_work);
	}
}

struct diff_storage *diff_storage_new(void)
{
	struct diff_storage *diff_storage;

	diff_storage = kzalloc(sizeof(struct diff_storage), GFP_KERNEL);
	if (!diff_storage)
		return NULL;

	kref_init(&diff_storage->kref);
	spin_lock_init(&diff_storage->lock);
	diff_storage->limit = 0;

	INIT_WORK(&diff_storage->reallocate_work, diff_storage_reallocate_work);
	event_queue_init(&diff_storage->event_queue);

	return diff_storage;
}

void diff_storage_free(struct kref *kref)
{
	struct diff_storage *diff_storage;

	diff_storage = container_of(kref, struct diff_storage, kref);
	flush_work(&diff_storage->reallocate_work);

#if defined(CONFIG_BLKSNAP_DIFF_BLKDEV)
	if (diff_storage->bdev) {
#if defined(HAVE_BLK_HOLDER_OPS)
		blkdev_put(diff_storage->bdev, diff_storage);
#else
		blkdev_put(diff_storage->bdev,
			   FMODE_EXCL | FMODE_READ | FMODE_WRITE);
#endif
	}
#endif
	if (diff_storage->file)
		filp_close(diff_storage->file, NULL);
	event_queue_done(&diff_storage->event_queue);
	kfree(diff_storage);
}

static inline int diff_storage_set_bdev(struct diff_storage *diff_storage,
					const char *devpath)
{
	struct block_device *bdev;

#if defined(HAVE_BLK_HOLDER_OPS)
#ifdef HAVE_BLK_OPEN_MODE
	bdev = blkdev_get_by_path(devpath,
				BLK_OPEN_EXCL | BLK_OPEN_READ | BLK_OPEN_WRITE,
				diff_storage, NULL);
#else
	bdev = blkdev_get_by_path(devpath,
				FMODE_EXCL | FMODE_READ | FMODE_WRITE,
				diff_storage, NULL);
#endif
#else
	bdev = blkdev_get_by_path(devpath,
				FMODE_EXCL | FMODE_READ | FMODE_WRITE,
				diff_storage);
#endif
	if (IS_ERR(bdev)) {
		pr_err("Failed to open a block device '%s'\n", devpath);
		return PTR_ERR(bdev);
	}

	pr_debug("A block device is selected for difference storage\n");
	diff_storage->dev_id = bdev->bd_dev;
	diff_storage->capacity = bdev_nr_sectors(bdev);
	diff_storage->bdev = bdev;
	return 0;
}

static inline void ___set_file(struct diff_storage *diff_storage,
			       struct file *file)
{
	struct inode *inode = file_inode(file);

	diff_storage->dev_id = inode->i_sb->s_dev;
	diff_storage->capacity = i_size_read(inode) >> SECTOR_SHIFT;
	diff_storage->file = file;
}

static inline int diff_storage_set_tmpfile(struct diff_storage *diff_storage,
					   const char *dirname)
{
	struct file *file;
	int flags = O_EXCL | O_RDWR | O_LARGEFILE | O_DIRECT | O_NOATIME |
		    O_TMPFILE;

	file = filp_open(dirname, flags, S_IRUSR | S_IWUSR);
	if (IS_ERR(file)) {
		pr_err("Failed to create a temp file in directory '%s'\n",
			dirname);
		return PTR_ERR(file);
	}

	pr_debug("A temp file is selected for difference storage\n");
	___set_file(diff_storage, file);
	return 0;
}

static inline int diff_storage_set_regfile(struct diff_storage *diff_storage,
					   const char *filename)
{
	struct file *file;
	int flags = O_EXCL | O_RDWR | O_LARGEFILE | O_DIRECT | O_NOATIME;

	file = filp_open(filename, flags, S_IRUSR | S_IWUSR);
	if (IS_ERR(file)) {
		pr_err("Failed to open a regular file '%s'\n", filename);
		return PTR_ERR(file);
	}

	pr_debug("A regular file is selected for difference storage\n");
	___set_file(diff_storage, file);
	return 0;
}

int diff_storage_set_diff_storage(struct diff_storage *diff_storage,
				  const char *filename, sector_t limit)
{
	int ret = 0;
	struct file *file;
	umode_t mode;
	sector_t req_sect;

	file = filp_open(filename, O_RDONLY, S_IRUSR);
	if (!file) {
		pr_err("Failed to open '%s'\n", filename);
		return -EINVAL;
	}
	mode = file_inode(file)->i_mode;
	__fput_sync(file);

	if (S_ISBLK(mode))
		ret = diff_storage_set_bdev(diff_storage, filename);
	else if (S_ISDIR(mode))
		ret = diff_storage_set_tmpfile(diff_storage, filename);
	else if (S_ISREG(mode))
		ret = diff_storage_set_regfile(diff_storage, filename);
	else {
		pr_err("The difference storage should be a block device, directory or regular file\n");
		ret = -EINVAL;
	}
	if (ret)
		return ret;

	diff_storage->requested = diff_storage->capacity;
	diff_storage->limit = limit;

	if (!is_halffull(diff_storage->requested))
		return 0;

	if (diff_storage->capacity == diff_storage->limit) {
		pr_info("The limit size of the difference storage has been reached\n");
		return 0;
	}
	if (diff_storage->capacity > diff_storage->limit) {
		pr_err("The limit size of the difference storage has been exceeded\n");
		return -ENOSPC;
	}

	diff_storage->requested +=
		min(get_diff_storage_minimum(),
		    diff_storage->limit - diff_storage->capacity);
	req_sect = diff_storage->requested;

	if (diff_storage->bdev) {
		pr_warn("Difference storage on block device is not large enough\n");
		pr_warn("Requested: %llu sectors\n", req_sect);
		return 0;
	}

	pr_debug("Difference storage is not large enough\n");
	pr_debug("Requested: %llu sectors\n", req_sect);

	ret = vfs_fallocate(diff_storage->file, 0, 0,
			    (loff_t)(req_sect << SECTOR_SHIFT));
	if (ret) {
		pr_err("Failed to fallocate difference storage file\n");
		pr_warn("The difference storage is not large enough\n");
		return ret;
	}
	diff_storage->capacity = req_sect;
	return 0;
}

int diff_storage_alloc(struct diff_storage *diff_storage, sector_t count,
#if defined(CONFIG_BLKSNAP_DIFF_BLKDEV)
			struct block_device **bdev,
#endif
			struct file **file, sector_t *sector)

{
	sector_t sectors_left;

	if (atomic_read(&diff_storage->overflow_flag))
		return -ENOSPC;

	spin_lock(&diff_storage->lock);
	if ((diff_storage->filled + count) > diff_storage->requested) {
		atomic_inc(&diff_storage->overflow_flag);
		spin_unlock(&diff_storage->lock);
		return -ENOSPC;
	}

#if defined(CONFIG_BLKSNAP_DIFF_BLKDEV)
	*bdev = diff_storage->bdev;
#endif
	*file = diff_storage->file;
	*sector = diff_storage->filled;

	diff_storage->filled += count;
	sectors_left = diff_storage->requested - diff_storage->filled;

	spin_unlock(&diff_storage->lock);

	check_halffull(diff_storage, sectors_left);
	return 0;
}
