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
#ifdef BLKSNAP_FILELOG
#include "log.h"
#endif
#ifdef BLKSNAP_MEMSTAT
#include "memstat.h"
#endif

#ifdef BLKSNAP_MODIFICATION

struct diff_storage_range {
	struct list_head link;
	dev_t dev_id;
	sector_t ofs;
	sector_t cnt;
};

static inline void diff_storage_event_low(struct diff_storage *diff_storage, sector_t req_sect)
{
	struct blksnap_event_low_free_space data = {
		.requested_nr_sect = req_sect,
	};

	diff_storage->requested += data.requested_nr_sect;
	pr_debug("Diff storage low free space. Portion: %llu sectors, requested: %llu\n",
		data.requested_nr_sect, diff_storage->requested);
	event_gen(&diff_storage->event_queue,
		  blksnap_event_code_low_free_space,
		  &data,
		  sizeof(data));
}

#endif

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
		if (complete) {
			atomic_set(&diff_storage->low_space_flag, 0);
			pr_debug("Skip low_space_flag\n");
		}
		spin_unlock(&diff_storage->lock);

		pr_debug("Diff storage reallocate. Capacity: %llu sectors\n",
			 req_sect);
	} while (!complete);
}

static sector_t diff_storage_calculate_requested(struct diff_storage *diff_storage)
{
	sector_t req_sect = 0;

	spin_lock(&diff_storage->lock);
	if (diff_storage->requested < diff_storage->limit) {
		req_sect = min(get_diff_storage_minimum(),
				diff_storage->limit - diff_storage->requested);
		diff_storage->requested += req_sect;
	}
	pr_debug("The size of the difference storage was %llu MiB\n",
		 diff_storage->capacity >> (20 - SECTOR_SHIFT));
	pr_debug("The size already requested is %llu MiB\n",
		 diff_storage->requested >> (20 - SECTOR_SHIFT));
	pr_debug("The limit is %llu MiB\n",
		 diff_storage->limit >> (20 - SECTOR_SHIFT));
	spin_unlock(&diff_storage->lock);

	return req_sect;
}

static inline bool is_halffull(const sector_t sectors_left)
{
	return sectors_left <= (get_diff_storage_minimum() / 2);
}

static inline void check_halffull(struct diff_storage *diff_storage,
				  const sector_t sectors_left)
{
	sector_t req_sect;

	if (is_halffull(sectors_left) &&
	    (atomic_inc_return(&diff_storage->low_space_flag) == 1)) {
	    	pr_debug("Set low_space_flag\n");

#ifdef BLKSNAP_MODIFICATION
		if (!diff_storage->bdev_holder && !diff_storage->file) {
			req_sect = diff_storage_calculate_requested(diff_storage);
			if (req_sect)
				diff_storage_event_low(diff_storage, req_sect);
			else
				pr_info("The limit size of the difference storage has been reached\n");
			return;
		}
#endif
		if (diff_storage->bdev_holder) {
			pr_warn("Reallocating is allowed only for a regular file\n");
			return;
		}
		if (!diff_storage_calculate_requested(diff_storage)) {
			pr_info("The limit size of the difference storage has been reached\n");
			return;
		}

		pr_debug("Diff storage low free space.\n");
		blksnap_queue_work(&diff_storage->reallocate_work);
	}
}

struct diff_storage *diff_storage_new(void)
{
	struct diff_storage *diff_storage;

	diff_storage = ms_kzalloc(sizeof(struct diff_storage), GFP_KERNEL);
	if (!diff_storage)
		return NULL;

	kref_init(&diff_storage->kref);
	spin_lock_init(&diff_storage->lock);
	diff_storage->limit = 0;

	INIT_WORK(&diff_storage->reallocate_work, diff_storage_reallocate_work);
	event_queue_init(&diff_storage->event_queue);

#ifdef BLKSNAP_MODIFICATION
	xa_init(&diff_storage->diff_storage_bdev_map);
	spin_lock_init(&diff_storage->ranges_lock);
	INIT_LIST_HEAD(&diff_storage->free_ranges_list);
#endif
	return diff_storage;
}

void diff_storage_free(struct kref *kref)
{
	struct diff_storage *diff_storage;

	diff_storage = container_of(kref, struct diff_storage, kref);
	flush_work(&diff_storage->reallocate_work);

	if (diff_storage->bdev_holder)
		bdev_close_excl(diff_storage->bdev_holder, diff_storage);

	if (diff_storage->file)
		filp_close(diff_storage->file, NULL);
	event_queue_done(&diff_storage->event_queue);

#ifdef BLKSNAP_MODIFICATION
	while (!list_empty(&diff_storage->free_ranges_list)) {
		struct diff_storage_range *rg = NULL;

		rg = list_first_entry(&diff_storage->free_ranges_list,
				      struct diff_storage_range, link);
		list_del(&rg->link);
		ms_kfree(rg);
	}
	{
		unsigned long dev_id;
		bdev_holder_t *diff_storage_bdev;

		xa_for_each(&diff_storage->diff_storage_bdev_map, dev_id,
							diff_storage_bdev)
			bdev_close(diff_storage_bdev);

	}
	xa_destroy(&diff_storage->diff_storage_bdev_map);
#endif /* BLKSNAP_MODIFICATION */

	ms_kfree(diff_storage);
}

static inline int diff_storage_set_bdev(struct diff_storage *diff_storage,
					const char *devpath)
{
	int ret;
	bdev_holder_t *bdev_holder;
	struct block_device *bdev;

	ret = bdev_open_excl(devpath, diff_storage, &bdev_holder, &bdev);
	if (ret) {
		pr_err("Failed to open a block device '%s'\n", devpath);
		return ret;
	}
	pr_debug("A block device is selected for difference storage\n");
	diff_storage->bdev_holder = bdev_holder;
	diff_storage->dev_id = bdev->bd_dev;
	diff_storage->capacity = bdev_nr_sectors(bdev);

	return 0;
}

static inline void ___set_file(struct diff_storage *diff_storage,
			       struct file *file)
{
	struct inode *inode = file_inode(file);

	/*
	 * Blocks the ability to place the difference storage on a block
	 * device under the snapshot.
	 */
	diff_storage->dev_id = inode->i_sb->s_dev;
	diff_storage->capacity = i_size_read(inode) >> SECTOR_SHIFT;
	diff_storage->file = file;
}

static inline int diff_storage_set_tmpfile(struct diff_storage *diff_storage,
					   const char *dirname)
{
	struct file *file;
	int flags = O_EXCL | O_RDWR | O_LARGEFILE | O_NOATIME | O_DIRECT |
		    O_TMPFILE;

	file = filp_open(dirname, flags, 00600);
	if (IS_ERR(file)) {
		if (PTR_ERR(file) == -EINVAL) {
			pr_warn("Failed to create a temp file '%s' with O_DIRECT flag\n", dirname);
			file = filp_open(dirname, flags & ~O_DIRECT, 00600);
		}
		if (IS_ERR(file)) {
			pr_err("Failed to create a temp file in directory '%s'\n",
				dirname);
			return PTR_ERR(file);
		}
	}

	pr_debug("A temp file is selected for difference storage\n");
	___set_file(diff_storage, file);
	return 0;
}

static inline int diff_storage_set_regfile(struct diff_storage *diff_storage,
					   const char *filename)
{
	struct file *file;
	int flags = O_EXCL | O_RDWR | O_LARGEFILE | O_NOATIME | O_DIRECT;

	file = filp_open(filename, flags, 0);
	if (IS_ERR(file)) {
		if (PTR_ERR(file) == -EINVAL) {
			pr_warn("Failed to open a regular file '%s' with O_DIRECT flag\n", filename);
			file = filp_open(filename, flags & ~O_DIRECT, 0);
		}
		if (IS_ERR(file)) {
			pr_err("Failed to open a regular file '%s'\n", filename);
			return PTR_ERR(file);
		}
	}

	pr_debug("A regular file is selected for difference storage\n");
	___set_file(diff_storage, file);
	return 0;
}

int diff_storage_set(struct diff_storage *diff_storage, const char *filename,
		     sector_t limit)
{
	int ret = 0;
	struct file *file;
	umode_t mode;
	sector_t req_sect;

#if defined(BLKSNAP_MODIFICATION)
	if (!filename) {
		diff_storage->limit = limit;

	    	atomic_inc(&diff_storage->low_space_flag);
	    	pr_debug("Set low_space_flag\n");

	    	req_sect = diff_storage_calculate_requested(diff_storage);
		if (req_sect)
			diff_storage_event_low(diff_storage, req_sect);
		else
			pr_info("The limit size of the difference storage has been reached\n");
		return 0;
	}
#endif

	file = filp_open(filename, O_RDONLY | O_LARGEFILE | O_NOATIME, 0);
	if (IS_ERR(file)) {
		pr_err("Failed to open '%s'\n", filename);
		return PTR_ERR(file);
	}
	mode = file_inode(file)->i_mode;
	filp_close(file, NULL);

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

	if (diff_storage->bdev_holder) {
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
			struct block_device **pbdev, struct file **pfile,
			sector_t *psector)

{
	int ret = 0;
	sector_t sectors_left = 0;

	if (atomic_read(&diff_storage->overflow_flag))
		return -ENOSPC;

	spin_lock(&diff_storage->lock);
	if ((diff_storage->filled + count) > diff_storage->requested) {
		atomic_inc(&diff_storage->overflow_flag);
		ret = -ENOSPC;
		goto out_spin_unlock;
	}

	if (diff_storage->bdev_holder) {
#if defined(HAVE_BDEV_FILE_OPEN)
	*pbdev = file_bdev(diff_storage->bdev_holder);
#elif defined(HAVE_BDEV_HANDLE)
	*pbdev = diff_storage->bdev_holder->bdev;
#else
	*pbdev = diff_storage->bdev_holder;
#endif
	}
	*pfile = diff_storage->file;
	*psector = diff_storage->filled;
#ifdef BLKSNAP_MODIFICATION
	if (!*pbdev && ! *pfile) {
		ret = diff_storage_get_range(diff_storage, count, pbdev, psector);
		if (ret)
			goto out_spin_unlock;
	}
#endif

	diff_storage->filled += count;
	sectors_left = diff_storage->requested - diff_storage->filled;
out_spin_unlock:
	spin_unlock(&diff_storage->lock);
	if (!ret)
		check_halffull(diff_storage, sectors_left);
	return ret;
}

#ifdef BLKSNAP_MODIFICATION

int diff_storage_add_bdev(struct diff_storage *diff_storage,
			   bdev_holder_t *bdev_holder)
{
	dev_t dev_id;

	dev_id = bdev_id_by_holder(bdev_holder);
	if (xa_load(&diff_storage->diff_storage_bdev_map, dev_id))
		return -EALREADY;

	return xa_insert(&diff_storage->diff_storage_bdev_map, dev_id,
			bdev_holder, GFP_KERNEL);
}

int diff_storage_add_range(struct diff_storage *diff_storage,
			   dev_t dev_id,
			   struct blksnap_sectors range)
{
	struct diff_storage_range *rg;

	rg = ms_kzalloc(sizeof(struct diff_storage_range), GFP_KERNEL);
	if (!rg)
		return -ENOMEM;

	INIT_LIST_HEAD(&rg->link);
	rg->dev_id = dev_id;
	rg->ofs = range.offset;
	rg->cnt = range.count;

	spin_lock(&diff_storage->ranges_lock);
	list_add_tail(&rg->link, &diff_storage->free_ranges_list);
	diff_storage->capacity += range.count;
	spin_unlock(&diff_storage->ranges_lock);
	return 0;
}

int diff_storage_get_range(struct diff_storage *diff_storage, sector_t count,
			   struct block_device **pbdev, sector_t *poffset)
{
	dev_t dev_id = 0;
	bdev_holder_t *diff_storage_bdev;
	struct diff_storage_range *rg = NULL;

	spin_lock(&diff_storage->ranges_lock);
	while (!list_empty(&diff_storage->free_ranges_list)) {
		rg = list_first_entry(&diff_storage->free_ranges_list,
				      struct diff_storage_range, link);
		if (rg->cnt >= count) {
			dev_id = rg->dev_id;
			*poffset = rg->ofs;

			rg->ofs += count;
			rg->cnt -= count;
			break;
		}

		list_del(&rg->link);
		ms_kfree(rg);
	}
	spin_unlock(&diff_storage->ranges_lock);

	if (dev_id == 0)
		return -ENOSPC;

	diff_storage_bdev = xa_load(&diff_storage->diff_storage_bdev_map, dev_id);
	if (!diff_storage_bdev) {
		pr_err("Failed to get difference storage block device\n");
		return -EFAULT;
	}

	*pbdev = bdev_by_holder(diff_storage_bdev);
	return 0;
}

#endif
