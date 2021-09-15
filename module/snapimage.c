// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME "-snapimage: " fmt
#include <linux/slab.h>
#include <linux/cdrom.h>
#include <linux/blk-mq.h>
#include "blk_snap.h"
#include "snapimage.h"
#include "diff_area.h"
#include "cbt_map.h"

#define SNAPIMAGE_MAX_DEVICES 2048

int snapimage_major;
unsigned long *snapimage_minors;
DEFINE_SPINLOCK(snapimage_minors_lock);

static 
blk_status_t snapimage_rq_io(struct snapimage *snapimage, struct request *rq)
{
	blk_status_t status = BLK_STS_OK;
	struct bio_vec bvec;
	struct req_iterator iter;
	struct diff_area_image_ctx io_ctx;
	sector_t pos = blk_rq_pos(rq);

	diff_area_image_ctx_init(&io_ctx, snapimage->diff_area,
				 op_is_write(req_op(rq)));

	rq_for_each_segment(bvec, rq, iter) {
		status = diff_area_image_io(&io_ctx, &bvec, &pos);
		if (unlikely(status != BLK_STS_OK))
			break;
	}
	diff_area_image_ctx_done(&io_ctx);

	return status;
}

static 
blk_status_t snapimage_queue_rq(struct blk_mq_hw_ctx *hctx,
		const struct blk_mq_queue_data *bd)
{
	int ret = 0;
	blk_status_t status;
	struct request *rq = bd->rq;
	struct snapimage *snapimage = rq->q->queuedata;

	blk_mq_start_request(rq);
	if (op_is_write(req_op(rq))) {
		ret = cbt_map_set_both(snapimage->cbt_map, blk_rq_pos(rq),
				       blk_rq_sectors(rq));
		if (unlikely(ret)) {
			status = BLK_STS_IOERR;
			goto out;
		}
	}
	status = snapimage_rq_io(snapimage, rq);
out:
	blk_mq_end_request(rq, status);

	return status;
}

static const
struct blk_mq_ops mq_ops = {
	.queue_rq = snapimage_queue_rq,
};

const
struct block_device_operations bd_ops = {
	.owner = THIS_MODULE,
	//.open = snapimage_open,
	//.ioctl = snapimage_ioctl,
	//.release = snapimage_close,
};


void snapimage_free(struct snapimage *snapimage)
{
	pr_info("Snapshot image disk delete\n");

	diff_area_put(snapimage->diff_area);
	cbt_map_put(snapimage->cbt_map);

	del_gendisk(snapimage->disk);
#ifdef HAVE_BLK_MQ_ALLOC_DISK
	blk_cleanup_disk(snapimage->disk);
#else
	blk_cleanup_queue(snapimage->queue);
	put_disk(snapimage->disk);
#endif
	blk_mq_free_tag_set(&snapimage->tag_set);

	spin_lock(&snapimage_minors_lock);
	bitmap_clear(snapimage_minors, MINOR(snapimage->image_dev_id), 1u);
	spin_unlock(&snapimage_minors_lock);

	kfree(snapimage);
}

static
void snapimage_tagset_init(struct blk_mq_tag_set *tag_set, void *snapimage)
{
	tag_set->ops = &mq_ops;
	tag_set->nr_hw_queues = 1;
	tag_set->queue_depth = 128;
	tag_set->numa_node = NUMA_NO_NODE;
	//tag_set->cmd_size = sizeof(struct loop_cmd);
	tag_set->flags = BLK_MQ_F_SHOULD_MERGE | BLK_MQ_F_STACKING;
	tag_set->driver_data = snapimage;
}

struct snapimage *snapimage_create(struct diff_area *diff_area,
                                   struct cbt_map *cbt_map)
{
	int ret = 0;
	int minor;
	struct snapimage *snapimage = NULL;
	struct gendisk *disk;
#ifndef HAVE_BLK_MQ_ALLOC_DISK
	struct request_queue *queue;
#endif

	pr_info("Create snapshot image for device [%u:%u]\n",
	        MAJOR(diff_area->orig_bdev->bd_dev),
	        MINOR(diff_area->orig_bdev->bd_dev));

	snapimage = kzalloc(sizeof(struct snapimage), GFP_KERNEL);
	if (snapimage == NULL)
		return ERR_PTR(-ENOMEM);

	spin_lock(&snapimage_minors_lock);
	minor = bitmap_find_free_region(snapimage_minors, SNAPIMAGE_MAX_DEVICES, 0);
	spin_unlock(&snapimage_minors_lock);

	if (minor < 0) {
		ret = abs(minor);
		pr_err("Failed to allocate minor for snapshot image device. errno=%d\n",
			ret);
		goto fail_free_image;
	}

	snapimage->capacity = cbt_map->device_capacity;
	snapimage->image_dev_id = MKDEV(snapimage_major, minor);
	pr_info("Snapshot image device id [%u:%u]\n",
	        MAJOR(snapimage->image_dev_id),
	        MINOR(snapimage->image_dev_id));

	snapimage_tagset_init(&snapimage->tag_set, snapimage);

	ret = blk_mq_alloc_tag_set(&snapimage->tag_set);
	if (ret) {
		pr_err("Failed to allocate tag set. errno=%d\n", ret);
		goto fail_free_image;
	}

#ifdef HAVE_BLK_MQ_ALLOC_DISK
	disk = blk_mq_alloc_disk(&snapimage->tag_set, snapimage);
	if (IS_ERR(disk)) {
		ret = PTR_ERR(disk);
		pr_err("Failed to allocate disk. errno=%d\n", ret);
		goto fail_free_tagset;
	}
#else
	queue = blk_mq_init_queue(&snapimage->tag_set);
	if (IS_ERR(queue)) {
		ret = PTR_ERR(queue);
		pr_err("Failed to allocate queue. errno=%d\n", ret);
		goto fail_free_tagset;
	}
	snapimage->queue = queue;
	snapimage->queue->queuedata = snapimage;

	disk = alloc_disk(1);
	if (IS_ERR(disk)) {
		ret = PTR_ERR(disk);
		pr_err("Failed to allocate disk. errno=%d\n", ret);
		goto fail_free_queue;
	}
	disk->queue = queue;
#endif
	snapimage->disk = disk;

	if (snprintf(disk->disk_name, DISK_NAME_LEN, "%s%d", SNAPIMAGE_NAME, minor) < 0) {
		pr_err("Unable to set disk name for snapshot image device: invalid minor %d\n",
		       minor);
		ret = -EINVAL;
		goto fail_cleanup_disk;
	}
	pr_info("Snapshot image disk name [%s]", disk->disk_name);

	//disk->flags |= GENHD_FL_SUPPRESS_PARTITION_INFO;
	//disk->flags |= GENHD_FL_EXT_DEVT;
	disk->flags |= GENHD_FL_NO_PART_SCAN;
	disk->flags |= GENHD_FL_HIDDEN;
	//disk->flags |= GENHD_FL_REMOVABLE;

	disk->major = snapimage_major;
	disk->minors = 1; // one disk have only one partition.
	disk->first_minor = minor;

	disk->fops = &bd_ops;
	disk->private_data = snapimage;

	set_capacity(disk, snapimage->capacity);
	pr_info("Snapshot image device capacity %lld bytes",
		(u64)(snapimage->capacity << SECTOR_SHIFT));

	diff_area_get(diff_area);
	snapimage->diff_area = diff_area;
	cbt_map_get(cbt_map);
	snapimage->cbt_map = cbt_map;

	add_disk(disk);

	return snapimage;

fail_cleanup_disk:
#ifdef HAVE_BLK_MQ_ALLOC_DISK
	blk_cleanup_disk(disk);
#else
	del_gendisk(disk);
fail_free_queue:
	blk_cleanup_queue(queue);
#endif
fail_free_tagset:
	blk_mq_free_tag_set(&snapimage->tag_set);
fail_free_image:
	kfree(snapimage);
	return ERR_PTR(ret);
}

int snapimage_init(void)
{
	int major = 0;

	snapimage_minors = bitmap_zalloc(SNAPIMAGE_MAX_DEVICES, GFP_KERNEL);
	if (!snapimage_minors) {
		pr_err("Failed to initialize bitmap of minors\n");
		return -ENOMEM;
	}

	major = register_blkdev(snapimage_major, SNAPIMAGE_NAME);
	if (major < 0) {
		pr_err("Failed to register snapshot image block device. errno=%d\n",
			abs(major));
		bitmap_free(snapimage_minors);
		return major;
	}
	snapimage_major = major;
	pr_info("Snapshot image block device major %d was registered\n",
		snapimage_major);
	
	return 0;
}

void snapimage_done(void)
{
	bitmap_free(snapimage_minors);
	snapimage_minors = NULL;

	unregister_blkdev(snapimage_major, SNAPIMAGE_NAME);
	pr_info("Snapshot image block device [%d] was unregistered\n",
		snapimage_major);
}
