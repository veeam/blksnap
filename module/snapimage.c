// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME "-snapimage: " fmt

#include <linux/slab.h>
#include <linux/cdrom.h>
#include <linux/blk-mq.h>
#ifdef STANDALONE_BDEVFILTER
#include "blksnap.h"
#else
#include <uapi/linux/blksnap.h>
#endif
#include "memory_checker.h"
#include "snapimage.h"
#include "diff_area.h"
#include "chunk.h"
#include "cbt_map.h"
#ifdef STANDALONE_BDEVFILTER
#include "log.h"
#endif

static void snapimage_process_bio(struct snapimage *snapimage, struct bio *bio)
{

	struct diff_area_image_ctx io_ctx;
	struct bio_vec bvec;
	struct bvec_iter iter;
	sector_t pos = bio->bi_iter.bi_sector;

	diff_area_throttling_io(snapimage->diff_area);
	diff_area_image_ctx_init(&io_ctx, snapimage->diff_area,
				 op_is_write(bio_op(bio)));
	bio_for_each_segment(bvec, bio, iter) {
		blk_status_t st;

		st = diff_area_image_io(&io_ctx, &bvec, &pos);
		if (unlikely(st != BLK_STS_OK))
			break;
	}
	diff_area_image_ctx_done(&io_ctx);
	bio_endio(bio);
}

static inline struct bio *get_bio_from_queue(struct snapimage *snapimage)
{
	struct bio *bio;

	spin_lock(&snapimage->queue_lock);
	bio = bio_list_pop(&snapimage->queue);
	spin_unlock(&snapimage->queue_lock);

	return bio;
}

static int snapimage_kthread_worker_fn(void *param)
{
	struct snapimage *snapimage = param;
	struct bio *bio;
#if 0
	/*
	 * Old implementation should be removed. TODO
	 */
	while (!kthread_should_stop()) {
		bio = get_bio_from_queue(snapimage);
		if (!bio) {
			schedule_timeout_interruptible(HZ / 100);
			continue;
		}
		snapimage_process_bio(snapimage, bio);
	}
	while ((bio = get_bio_from_queue(snapimage)))
		snapimage_process_bio(snapimage, bio);
#else
	for (;;) {
		while ((bio = get_bio_from_queue(snapimage)))
			snapimage_process_bio(snapimage, bio);
		if (kthread_should_stop())
			break;
		schedule();
	}
#endif
	return 0;
}

#ifdef HAVE_QC_SUBMIT_BIO
static blk_qc_t snapimage_submit_bio(struct bio *bio)
{
	blk_qc_t ret = BLK_QC_T_NONE;
#else
static void snapimage_submit_bio(struct bio *bio)
{
#endif
#ifdef HAVE_BI_BDEV
	struct snapimage *snapimage = bio->bi_bdev->bd_disk->private_data;
#endif
#ifdef HAVE_BI_BDISK
	struct snapimage *snapimage = bio->bi_disk->private_data;
#endif

	if (!diff_area_is_corrupted(snapimage->diff_area)) {
		spin_lock(&snapimage->queue_lock);
		bio_list_add(&snapimage->queue, bio);
		spin_unlock(&snapimage->queue_lock);

		wake_up_process(snapimage->worker);
	} else
		bio_io_error(bio);

#ifdef HAVE_QC_SUBMIT_BIO
	return ret;
}
#else
}
#endif

static void snapimage_free_disk(struct gendisk *disk)
{
	struct snapimage *snapimage = disk->private_data;

	diff_area_put(snapimage->diff_area);
	cbt_map_put(snapimage->cbt_map);

	kfree(snapimage);
	memory_object_dec(memory_object_snapimage);
}

const struct block_device_operations bd_ops = {
	.owner = THIS_MODULE,
	.submit_bio = snapimage_submit_bio,
	.free_disk = snapimage_free_disk,
};

void snapimage_free(struct snapimage *snapimage)
{
	pr_info("Snapshot image disk %s delete\n", snapimage->disk->disk_name);

	del_gendisk(snapimage->disk);

	kthread_stop(snapimage->worker);
#ifdef HAVE_BLK_ALLOC_DISK
#ifdef HAVE_BLK_CLEANUP_DISK
	blk_cleanup_disk(snapimage->disk);
#else
	put_disk(snapimage->disk);
#endif
#else
	blk_cleanup_queue(snapimage->disk->queue);
	put_disk(snapimage->disk);
#endif
}

#ifndef HAVE_BLK_ALLOC_DISK
static inline struct gendisk *blk_alloc_disk(int node)
{
	struct request_queue *q;
	struct gendisk *disk;

	q = blk_alloc_queue(node);
	if (!q)
		return NULL;

	disk = __alloc_disk_node(0, node);
	if (!disk) {
		blk_cleanup_queue(q);
		return NULL;
	}
	disk->queue = q;

	return disk;
}
#endif

struct snapimage *snapimage_create(struct diff_area *diff_area,
				   struct cbt_map *cbt_map)
{
	int ret = 0;
	dev_t dev_id = diff_area->orig_bdev->bd_dev;
	struct snapimage *snapimage = NULL;
	struct gendisk *disk;
	struct task_struct *task;

	snapimage = kzalloc(sizeof(struct snapimage), GFP_KERNEL);
	if (snapimage == NULL)
		return ERR_PTR(-ENOMEM);
	memory_object_inc(memory_object_snapimage);

	snapimage->capacity = cbt_map->device_capacity;
	pr_info("Create snapshot image devicefor original device [%u:%u]\n",
		MAJOR(dev_id), MINOR(dev_id));

	spin_lock_init(&snapimage->queue_lock);
	bio_list_init(&snapimage->queue);

	task = kthread_create(snapimage_kthread_worker_fn, snapimage,
			      "blksnap_%d_%d",
			      MAJOR(dev_id), MINOR(dev_id));
	if (IS_ERR(task)) {
		ret = PTR_ERR(task);
		pr_err("Failed to start worker thread. errno=%d\n", abs(ret));
		goto fail_free_image;
	}

	snapimage->worker = task;
	set_user_nice(task, MAX_NICE);
	task->flags |= PF_LOCAL_THROTTLE | PF_MEMALLOC_NOIO;

	disk = blk_alloc_disk(NUMA_NO_NODE);
	if (!disk) {
		pr_err("Failed to allocate disk\n");
		ret = -ENOMEM;
		goto fail_free_worker;
	}
	snapimage->disk = disk;

	blk_queue_max_hw_sectors(disk->queue, BLK_DEF_MAX_SECTORS);
	blk_queue_flag_set(QUEUE_FLAG_NOMERGES, disk->queue);

	ret = snprintf(disk->disk_name, DISK_NAME_LEN, "%s_%d:%d",
		       BLK_SNAP_IMAGE_NAME, MAJOR(dev_id), MINOR(dev_id));
	if (ret < 0) {
		pr_err("Unable to set disk name for snapshot image device: invalid device id [%d:%d]\n",
		       MAJOR(dev_id),MINOR(dev_id));
		ret = -EINVAL;
		goto fail_cleanup_disk;
	}
	pr_debug("Snapshot image disk name [%s]\n", disk->disk_name);

	disk->flags = 0;
#ifdef STANDALONE_BDEVFILTER
#ifdef GENHD_FL_NO_PART_SCAN
	disk->flags |= GENHD_FL_NO_PART_SCAN;
#else
	disk->flags |= GENHD_FL_NO_PART;
#endif
#else
	disk->flags |= GENHD_FL_NO_PART;
#endif

	disk->fops = &bd_ops;
	disk->private_data = snapimage;

	set_capacity(disk, snapimage->capacity);
	pr_debug("Snapshot image device capacity %lld bytes\n",
		 (u64)(snapimage->capacity << SECTOR_SHIFT));

	diff_area_get(diff_area);
	snapimage->diff_area = diff_area;
	cbt_map_get(cbt_map);
	snapimage->cbt_map = cbt_map;

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

	return snapimage;

fail_cleanup_disk:
#ifdef HAVE_BLK_ALLOC_DISK
#ifdef HAVE_BLK_CLEANUP_DISK
	blk_cleanup_disk(disk);
#else
	put_disk(disk);
#endif
#else
	del_gendisk(disk);
#endif
fail_free_worker:
	kthread_stop(snapimage->worker);
fail_free_image:
	kfree(snapimage);
	memory_object_dec(memory_object_snapimage);
	return ERR_PTR(ret);
}

#ifdef BLK_SNAP_DEBUG_SECTOR_STATE
int snapimage_get_chunk_state(struct snapimage *snapimage, sector_t sector,
			      struct blk_snap_sector_state *state)
{
	int ret;

	ret = diff_area_get_sector_state(snapimage->diff_area, sector,
					 &state->chunk_state);
	if (ret)
		return ret;

	ret = cbt_map_get_sector_state(snapimage->cbt_map, sector,
				       &state->snap_number_prev,
				       &state->snap_number_curr);
	if (ret)
		return ret;
	{
		char buf[SECTOR_SIZE];

		ret = diff_area_get_sector_image(snapimage->diff_area, sector,
						 buf /*&state->buf*/);
		if (ret)
			return ret;

		pr_info("sector #%llu", sector);
		print_hex_dump(KERN_INFO, "data header: ", DUMP_PREFIX_OFFSET,
			       32, 1, buf, 96, true);
	}

	return 0;
}
#endif
