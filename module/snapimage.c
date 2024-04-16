// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Veeam Software Group GmbH */
/*
 * Present the snapshot image as a block device.
 */
#define pr_fmt(fmt) KBUILD_MODNAME "-image: " fmt
#include <linux/slab.h>
#include <linux/cdrom.h>
#include <linux/blk-mq.h>
#include <linux/build_bug.h>
#ifdef BLKSNAP_STANDALONE
#include "veeamblksnap.h"
#include "compat.h"
#else
#include <uapi/linux/blksnap.h>
#endif
#include "snapimage.h"
#include "tracker.h"
#include "chunk.h"
#include "cbt_map.h"
#ifdef BLKSNAP_FILELOG
#include "log.h"
#endif
#ifdef BLKSNAP_MEMSTAT
#include "memstat.h"
#endif
#ifdef BLKSNAP_HISTOGRAM
#include "log_histogram.h"
#endif

/*
 * The snapshot supports write operations.  This allows for example to delete
 * some files from the file system before backing up the volume. The data can
 * be stored only in the difference storage. Therefore, before partially
 * overwriting this data, it should be read from the original block device.
 */
#if defined(HAVE_QC_SUBMIT_BIO_NOACCT)
static blk_qc_t snapimage_submit_bio(struct bio *bio)
#else
static void snapimage_submit_bio(struct bio *bio)
#endif
{
#ifdef HAVE_BI_BDISK
	struct tracker *tracker = bio->bi_disk->private_data;
#else
	struct tracker *tracker = bio->bi_bdev->bd_disk->private_data;
#endif
	struct diff_area *diff_area = tracker->diff_area;
	unsigned int flags;
#if defined(BLKSNAP_STANDALONE)
	unsigned int processed = bio->bi_iter.bi_size;
#else
	struct blkfilter *prev_filter;
#endif
	bool is_success = true;

	/*
	 * We can use the diff_area here without fear that it will be released.
	 * The diff_area is not blocked from releasing now, because
	 * snapimage_free() is calling before diff_area_put() in
	 * tracker_release_snapshot().
	 */
	if (diff_area_is_corrupted(diff_area)) {
		bio_io_error(bio);
#if defined(HAVE_QC_SUBMIT_BIO_NOACCT)
		return BLK_QC_T_NONE;
#else
		return;
#endif
	}

	flags = memalloc_noio_save();
	/*
	 * The change tracking table should indicate that the image block device
	 * is different from the original device. At the next snapshot, such
	 * blocks must be inevitably reread.
	 */
	if (op_is_write(bio_op(bio)))
		cbt_map_set_both(tracker->cbt_map, bio->bi_iter.bi_sector,
				 bio_sectors(bio));
#if !defined(BLKSNAP_STANDALONE)
	prev_filter = current->blk_filter;
	current->blk_filter = &tracker->filter;
#endif
	while (bio->bi_iter.bi_size && is_success)
		is_success = diff_area_submit_chunk(diff_area, bio);
#ifdef BLKSNAP_STANDALONE
	processed = processed - bio->bi_iter.bi_size;
	if (processed) {
		atomic64_add(processed >> SECTOR_SHIFT,
			     op_is_write(bio_op(bio)) ?
		     		&diff_area->stat_image_written :
				&diff_area->stat_image_read);
	}
#endif
#if !defined(BLKSNAP_STANDALONE)
	current->blk_filter = prev_filter;
#endif
	if (is_success)
		bio_endio(bio);
	else
		bio_io_error(bio);
	memalloc_noio_restore(flags);
#if defined(HAVE_QC_SUBMIT_BIO_NOACCT)
	return BLK_QC_T_NONE;
#endif
}

static const struct block_device_operations bd_ops = {
	.owner = THIS_MODULE,
	.submit_bio = snapimage_submit_bio,
};

void snapimage_free(struct tracker *tracker)
{
	struct gendisk *disk = tracker->snap_disk;

	if (!disk)
		return;

	pr_debug("Snapshot image disk %s delete\n", disk->disk_name);
	del_gendisk(disk);
	put_disk(disk);

	tracker->snap_disk = NULL;
}

int snapimage_create(struct tracker *tracker)
{
	int ret = 0;
	dev_t dev_id = tracker->dev_id;
	struct gendisk *disk;
#ifndef HAVE_BLK_ALLOC_DISK
	struct request_queue *queue;
#endif

	pr_info("Create snapshot image device for original device [%u:%u]\n",
		MAJOR(dev_id), MINOR(dev_id));

#ifdef HAVE_BLK_ALLOC_DISK
#if defined(HAVE_BDEV_QUEUE_LIMITS)
	disk = blk_alloc_disk(NULL, NUMA_NO_NODE);
#else
	disk = blk_alloc_disk(NUMA_NO_NODE);
#endif
	if (!disk) {
		pr_err("Failed to allocate disk\n");
		return -ENOMEM;
	}
#else
	queue = blk_alloc_queue(NUMA_NO_NODE);
	if (!queue) {
		pr_err("Failed to allocate disk queue\n");
		return -ENOMEM;
	}

	disk = alloc_disk(1);
	if (!disk) {
		pr_err("Failed to allocate disk structure\n");
		ret = -ENOMEM;
		goto fail_cleanup_queue;
	}
	disk->queue = queue;
#endif

#ifdef GENHD_FL_NO_PART_SCAN
	disk->flags = GENHD_FL_NO_PART_SCAN;
#else
	disk->flags = GENHD_FL_NO_PART;
#endif
	disk->fops = &bd_ops;

	disk->private_data = tracker;
	set_capacity(disk, tracker->cbt_map->device_capacity);
	ret = snprintf(disk->disk_name, DISK_NAME_LEN, "%s_%d:%d",
		       BLKSNAP_IMAGE_NAME, MAJOR(dev_id), MINOR(dev_id));
	if (ret < 0) {
		pr_err("Unable to set disk name for snapshot image device: invalid device id [%d:%d]\n",
		       MAJOR(dev_id), MINOR(dev_id));
		ret = -EINVAL;
		goto fail_cleanup_disk;
	}
	pr_debug("Snapshot image disk name [%s]\n", disk->disk_name);

	blk_queue_physical_block_size(disk->queue,
					tracker->diff_area->physical_blksz);
	blk_queue_logical_block_size(disk->queue,
					tracker->diff_area->logical_blksz);
#ifdef HAVE_ADD_DISK_RESULT
	ret = add_disk(disk);
	if (ret) {
		pr_err("Failed to add disk [%s] for snapshot image device\n",
		       disk->disk_name);
		goto fail_cleanup_disk;
	}
#else
	add_disk(disk);
#endif
	tracker->snap_disk = disk;

	pr_debug("Image block device [%d:%d] has been created\n",
		disk->major, disk->first_minor);

	return 0;
fail_cleanup_disk:
#ifdef HAVE_BLK_ALLOC_DISK
#ifdef HAVE_BLK_CLEANUP_DISK
	blk_cleanup_disk(disk);
#else
	put_disk(disk);
#endif
#else
	del_gendisk(disk);
fail_cleanup_queue:
	blk_cleanup_queue(queue);
#endif
	return ret;
}
