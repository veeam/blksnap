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
#include <uapi/linux/blksnap.h>
#include "snapimage.h"
#include "tracker.h"
#include "chunk.h"
#include "cbt_map.h"

/*
 * The snapshot supports write operations.  This allows for example to delete
 * some files from the file system before backing up the volume. The data can
 * be stored only in the difference storage. Therefore, before partially
 * overwriting this data, it should be read from the original block device.
 */
static void snapimage_submit_bio(struct bio *bio)
{
	struct tracker *tracker = bio->bi_bdev->bd_disk->private_data;
	struct diff_area *diff_area = tracker->diff_area;
	unsigned int old_nofs;
	struct blkfilter *prev_filter;
	bool is_success = true;

	/*
	 * We can use the diff_area here without fear that it will be released.
	 * The diff_area is not blocked from releasing now, because
	 * snapimage_free() is calling before diff_area_put() in
	 * tracker_release_snapshot().
	 */
	if (diff_area_is_corrupted(diff_area)) {
		bio_io_error(bio);
		return;
	}

	/*
	 * The change tracking table should indicate that the image block device
	 * is different from the original device. At the next snapshot, such
	 * blocks must be inevitably reread.
	 */
	if (op_is_write(bio_op(bio)))
		cbt_map_set_both(tracker->cbt_map, bio->bi_iter.bi_sector,
				 bio_sectors(bio));

	prev_filter = current->blk_filter;
	current->blk_filter = &tracker->filter;
	old_nofs = memalloc_nofs_save();
	while (bio->bi_iter.bi_size && is_success)
		is_success = diff_area_submit_chunk(diff_area, bio);
	memalloc_nofs_restore(old_nofs);
	current->blk_filter = prev_filter;

	if (is_success)
		bio_endio(bio);
	else
		bio_io_error(bio);
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

	pr_info("Create snapshot image device for original device [%u:%u]\n",
		MAJOR(dev_id), MINOR(dev_id));

	disk = blk_alloc_disk(NUMA_NO_NODE);
	if (!disk) {
		pr_err("Failed to allocate disk\n");
		return -ENOMEM;
	}

	disk->flags = GENHD_FL_NO_PART;
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

	ret = add_disk(disk);
	if (ret) {
		pr_err("Failed to add disk [%s] for snapshot image device\n",
		       disk->disk_name);
		goto fail_cleanup_disk;
	}
	tracker->snap_disk = disk;

	pr_debug("Image block device [%d:%d] has been created\n",
		disk->major, disk->first_minor);

	return 0;

fail_cleanup_disk:
	put_disk(disk);
	return ret;
}
