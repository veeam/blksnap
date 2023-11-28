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
#include <uapi/linux/blksnap.h>
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
	if (diff_storage->bdev)
		blkdev_put(diff_storage->bdev, NULL);
#endif
	if (diff_storage->file)
		fput(diff_storage->file);
	event_queue_done(&diff_storage->event_queue);
	kfree(diff_storage);
}

static inline bool unsupported_mode(const umode_t m)
{
	return (S_ISCHR(m) || S_ISFIFO(m) || S_ISSOCK(m));
}

static inline bool unsupported_flags(const unsigned int flags)
{
	if (!(flags | O_RDWR)) {
		pr_err("Read and write access is required\n");
		return true;
	}
	if (!(flags | O_EXCL)) {
		pr_err("Exclusive access is required\n");
		return true;
	}

	return false;
}

int diff_storage_set_diff_storage(struct diff_storage *diff_storage,
				  unsigned int fd, sector_t limit)
{
	int ret = 0;
	struct file *file;

	file = fget(fd);
	if (!file) {
		pr_err("Invalid file descriptor\n");
		return -EINVAL;
	}

	if (unsupported_mode(file_inode(file)->i_mode)) {
		pr_err("The difference storage can only be a regular file or a block device\n");
		ret = -EINVAL;
		goto fail_fput;
	}

	if (unsupported_flags(file->f_flags)) {
		pr_err("Invalid flags 0x%x with which the file was opened\n",
			file->f_flags);
		ret = -EINVAL;
		goto fail_fput;
	}

	if (S_ISBLK(file_inode(file)->i_mode)) {
		struct block_device *bdev;
		dev_t dev_id = file_inode(file)->i_rdev;

		pr_debug("Open a block device %d:%d\n",
			MAJOR(dev_id), MINOR(dev_id));
		/*
		 * The block device is opened non-exclusively.
		 * It should be exclusive to open the file whose descriptor is
		 * passed to the module.
		 */
		bdev = blkdev_get_by_dev(dev_id,
					 BLK_OPEN_READ | BLK_OPEN_WRITE,
					 NULL, NULL);
		if (IS_ERR(bdev)) {
			pr_err("Cannot open a block device %d:%d\n",
				MAJOR(dev_id), MINOR(dev_id));
			ret = PTR_ERR(bdev);
			bdev = NULL;
			goto fail_fput;
		}

		pr_debug("A block device is selected for difference storage\n");
		diff_storage->dev_id = file_inode(file)->i_rdev;
		diff_storage->capacity = bdev_nr_sectors(bdev);
#if defined(CONFIG_BLKSNAP_DIFF_BLKDEV)
		diff_storage->bdev = bdev;
#else
		blkdev_put(bdev, NULL);
#endif
	} else {
		pr_debug("A regular file is selected for difference storage\n");
		diff_storage->dev_id = file_inode(file)->i_sb->s_dev;
		diff_storage->capacity =
				i_size_read(file_inode(file)) >> SECTOR_SHIFT;
	}

	diff_storage->file = get_file(file);
	diff_storage->requested = diff_storage->capacity;
	diff_storage->limit = limit;

	if (is_halffull(diff_storage->requested)) {
		sector_t req_sect;

		if (diff_storage->capacity == diff_storage->limit) {
			pr_info("The limit size of the difference storage has been reached\n");
			ret = 0;
			goto fail_fput;
		}
		if (diff_storage->capacity > diff_storage->limit) {
			pr_err("The limit size of the difference storage has been exceeded\n");
			ret = -ENOSPC;
			goto fail_fput;
		}

		diff_storage->requested += min(get_diff_storage_minimum(),
				diff_storage->limit - diff_storage->capacity);
		req_sect = diff_storage->requested;

#if defined(CONFIG_BLKSNAP_DIFF_BLKDEV)
		if (diff_storage->bdev) {
			pr_warn("Difference storage on block device is not large enough\n");
			pr_warn("Requested: %llu sectors\n", req_sect);
			ret = 0;
			goto fail_fput;
		}
#endif
		pr_debug("Difference storage is not large enough\n");
		pr_debug("Requested: %llu sectors\n", req_sect);

		ret = vfs_fallocate(diff_storage->file, 0, 0,
				    (loff_t)(req_sect << SECTOR_SHIFT));
		if (ret) {
			pr_err("Failed to fallocate difference storage file\n");
			pr_warn("The difference storage is not large enough\n");
			goto fail_fput;
		}
		diff_storage->capacity = req_sect;
	}
fail_fput:
	fput(file);
	return ret;
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
