// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-snapimage"
#include "snapimage.h"
#include "cbt_map.h"
#include "tracker.h"
#include <linux/cdrom.h>
#include <linux/blk-mq.h>

#define SNAPIMAGE_MAX_DEVICES 2048

int snapimage_major;
unsigned long *snapimage_minors;
DEFINE_SPINLOCK(snapimage_minors_lock);

static 
blk_status_t snapimage_rq_io(struct snapimage *image, struct request *rq)
{
	blk_status_t status = BLK_STS_OK;
	struct bio_vec bvec;
	struct req_iterator iter;
	struct diff_area_image_ctx io_ctx;
	sector_t pos = blk_rq_pos(rq);

	diff_area_image_ctx_init(&io_ctx, image->diff_area, op_is_write(req_op(rq)));

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
	blk_status_t status;
	struct request *rq = bd->rq;
	struct snapimage *image = rq->q->queuedata;

	blk_mq_start_request(rq);
	if (op_is_write(req_op(rq))) {
		ret = cbt_map_set_both(image->cbt_map, pos, blk_rq_sectors(rq));
		if (unlikely(ret))
			status = BLK_STS_IOERR;
		else
			status = snapimage_rq_io(image, rq);
	} else
		status = snapimage_read_rq(rq);
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
	.open = snapimage_open,
	.ioctl = snapimage_ioctl,
	.release = snapimage_close,
};


void snapimage_free(struct snapimage *image)
{
	pr_info("Snapshot image disk delete\n");

	del_gendisk(image->disk);
	blk_cleanup_disk(image->disk);
	blk_mq_free_tag_set(&image.tag_set);

	spin_lock(&snapimage_minors_lock);
	bitmap_clear(snapimage_minors, MINOR(image->image_dev_id), 1u);
	spin_unlock(&snapimage_minors_lock);

	kfree(image);
}

static
void snapimage_tagset_init(struct blk_mq_tag_set *tag_set, void *image)
{
	tag_set->ops = &mq_ops;
	tag_set->nr_hw_queues = 1;
	tag_set->queue_depth = 128;
	tag_set->numa_node = NUMA_NO_NODE;
	//tag_set->cmd_size = sizeof(struct loop_cmd);
	tag_set->flags = BLK_MQ_F_SHOULD_MERGE | BLK_MQ_F_STACKING;
	tag_set->driver_data = image;
}

struct snapimage *snapimage_create(struct diff_area *diff_area,
                                   struct cbt_map *cbt_map)
{
	int res = 0;
	struct snapimage *snapimage = NULL;
	struct request_queue *queue;
	struct gendisk *disk = NULL;

	int minor;

	pr_info("Create snapshot image for device [%d:%d]\n",
	        MAJOR(diff_area->orig_bdev->bd_dev),
	        MINOR(diff_area->orig_bdev->bd_dev));


	snapimage = kzalloc(sizeof(struct snapimage), GFP_KERNEL);
	if (snapimage == NULL)
		return -ENOMEM;

	INIT_LIST_HEAD(&snapimage->link);
	kref_init(&snapimage->kref);
	
	spin_lock(&snapimage_minors_lock);
	minor = bitmap_find_free_region(snapimage_minors, SNAPIMAGE_MAX_DEVICES, 0);
	spin_unlock(&snapimage_minors_lock);

	if (minor < 0) {
		pr_err("Failed to allocate minor for snapshot image device. errno=%d\n",
		       minor);
		goto fail_;
	}

	snapimage->capacity = cbt_map->device_capacity;
	snapimage->image_dev_id = MKDEV(snapimage_major, minor);
	pr_info("Snapshot image device id [%d:%d]\n",
	        MAJOR(snapimage->image_dev_id),
	        MINOR(snapimage->image_dev_id));

	snapimage_tagset_init(&snapimage.tag_set, snapimage);

	ret = blk_mq_alloc_tag_set(&snapimage.tag_set);
	if (ret)
		goto out_free_image;

	disk = blk_mq_alloc_disk(&snapimage.tag_set, snapimage);
	if (IS_ERR(disk)) {
		ret = PTR_ERR(disk);
		goto out_free_tagset;
	}

	if (snprintf(disk->disk_name, DISK_NAME_LEN, "%s%d", SNAPIMAGE_NAME, minor) < 0) {
		pr_err("Unable to set disk name for snapshot image device: invalid minor %d\n",
		       minor);
		res = -EINVAL;
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
	snapimage->disk = disk;

	set_capacity(disk, snapimage->capacity);
	pr_info("Snapshot image device capacity %lld bytes",
		(u64)from_sectors(snapimage->capacity));

	snapimage->diff_area = diff_area_get(diff_area);
	snapimage->cbt_map = cbt_map_get(cbt_map);

	add_disk(disk);

	return 0;

out_cleanup_disk:
	blk_cleanup_disk(disk);
out_free_tagset:
	blk_mq_free_tag_set(&snapimage->tag_set);
out_free_image:
	kfree(snapimage);
	return res;
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
		pr_err("Failed to register snapshot image block device. errno=%d\n", res);
		bitmap_free(snapimage_minors);
		return res;
	}
	snapimage_major = major;
	pr_info("Snapshot image block device major %d was registered\n", snapimage_major);
	
	return 0;
}

void snapimage_done(void)
{
	bitmap_free(snapimage_minors);
	snapimage_minors = NULL;

	unregister_blkdev(snapimage_major, SNAPIMAGE_NAME);
	pr_info("Snapshot image block device [%d] was unregistered\n", snapimage_major);
}
