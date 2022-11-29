// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME "-snapimage: " fmt
#include <linux/slab.h>
#include <linux/cdrom.h>
#include <linux/blk-mq.h>
#ifdef STANDALONE_BDEVFILTER
#include "blk_snap.h"
#else
#include <linux/blk_snap.h>
#endif
#include "memory_checker.h"
#include "snapimage.h"
#include "diff_area.h"
#include "chunk.h"
#include "cbt_map.h"
#ifdef STANDALONE_BDEVFILTER
#include "log.h"
#endif

#define NR_SNAPIMAGE_DEVT	(1 << MINORBITS)

static unsigned int _major;
static DEFINE_IDA(snapimage_devt_ida);

static int snapimage_kthread_worker_fn(void* param);

static inline struct snapimage_bio_link *get_bio_link_from_queue(
	spinlock_t *lock, struct list_head *queue)
{
	struct snapimage_bio_link *bio_link;

	spin_lock(lock);
	bio_link = list_first_entry_or_null(queue, struct snapimage_bio_link,
					    link);
	if (bio_link)
		list_del_init(&bio_link->link);
	spin_unlock(lock);

	return bio_link;
}

static inline struct snapimage_bio_link *get_bio_link_from_free_queue(
	struct snapimage *snapimage)
{
	return get_bio_link_from_queue(&snapimage->queue_lock,
				       &snapimage->free_queue);
}

static inline struct snapimage_bio_link *get_bio_link_from_todo_queue(
	struct snapimage *snapimage)
{
	return get_bio_link_from_queue(&snapimage->queue_lock,
				       &snapimage->todo_queue);
}

static inline void snapimage_stop_worker(struct snapimage *snapimage)
{
	struct snapimage_bio_link *bio_link;

	kthread_stop(snapimage->worker);
	put_task_struct(snapimage->worker);

	while ((bio_link = get_bio_link_from_free_queue(snapimage)))
		kfree(bio_link);
}

static inline int snapimage_start_worker(struct snapimage *snapimage)
{
	struct task_struct *task;

	spin_lock_init(&snapimage->queue_lock);
	INIT_LIST_HEAD(&snapimage->todo_queue);
	INIT_LIST_HEAD(&snapimage->free_queue);
	snapimage->free_queue_count = 0;

	task = kthread_create(snapimage_kthread_worker_fn,
			      snapimage,
			      BLK_SNAP_IMAGE_NAME "%d",
			      MINOR(snapimage->image_dev_id));
	if (IS_ERR(task))
		return -ENOMEM;

	snapimage->worker = get_task_struct(task);
	set_user_nice(task, MAX_NICE);
	task->flags |= PF_LOCAL_THROTTLE | PF_MEMALLOC_NOIO;
	wake_up_process(task);

	return 0;
}

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

static int snapimage_kthread_worker_fn(void* param)
{
	struct snapimage *snapimage = param;
	struct snapimage_bio_link *bio_link;
	int ret = 0;

	while (!kthread_should_stop()) {
		bio_link = get_bio_link_from_todo_queue(snapimage);
		if (!bio_link) {
			schedule_timeout_interruptible(HZ / 100);
			continue;
		}

		snapimage_process_bio(snapimage, bio_link->bio);

		spin_lock(&snapimage->queue_lock);
		if (snapimage->free_queue_count < 64) {
			bio_link->bio = NULL;
			list_add_tail(&bio_link->link, &snapimage->free_queue);
			snapimage->free_queue_count++;
			bio_link = NULL;
		}
		spin_unlock(&snapimage->queue_lock);

		if (bio_link)
			kfree(bio_link);
	}

	while ((bio_link = get_bio_link_from_todo_queue(snapimage))) {
		snapimage_process_bio(snapimage, bio_link->bio);
		kfree(bio_link);
	}
	return ret;
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
	struct snapimage_bio_link *bio_link;

	if (!snapimage->is_ready) {
		bio_io_error(bio);
		return ret;
	}

	spin_lock(&snapimage->queue_lock);
	bio_link = list_first_entry_or_null(&snapimage->free_queue,
					    struct snapimage_bio_link, link);
	if (bio_link) {
		list_del_init(&bio_link->link);
		snapimage->free_queue_count--;
	} else {
		spin_unlock(&snapimage->queue_lock);
		bio_link = kzalloc(sizeof(struct snapimage_bio_link), GFP_NOIO);
		INIT_LIST_HEAD(&bio_link->link);
		spin_lock(&snapimage->queue_lock);
	}

	if (bio_link) {
		bio_link->bio = bio;
		list_add_tail(&bio_link->link, &snapimage->todo_queue);
	}
	spin_unlock(&snapimage->queue_lock);

	if (bio_link)
		wake_up_process(snapimage->worker);
	else
		bio_io_error(bio);

#ifdef HAVE_QC_SUBMIT_BIO
	return ret;
}
#else
}
#endif

const struct block_device_operations bd_ops = {
	.owner = THIS_MODULE,
	.submit_bio = snapimage_submit_bio
};

void snapimage_free(struct snapimage *snapimage)
{
	pr_info("Snapshot image disk [%u:%u] delete\n",
		MAJOR(snapimage->image_dev_id), MINOR(snapimage->image_dev_id));

	blk_mq_freeze_queue(snapimage->disk->queue);
	snapimage->is_ready = false;
	blk_mq_unfreeze_queue(snapimage->disk->queue);

	snapimage_stop_worker(snapimage);

	del_gendisk(snapimage->disk);
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
	diff_area_put(snapimage->diff_area);
	cbt_map_put(snapimage->cbt_map);

	ida_free(&snapimage_devt_ida, MINOR(snapimage->image_dev_id));
	kfree(snapimage);
	memory_object_dec(memory_object_snapimage);
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
	int minor;
	struct snapimage *snapimage = NULL;
	struct gendisk *disk;

	snapimage = kzalloc(sizeof(struct snapimage), GFP_KERNEL);
	if (snapimage == NULL)
		return ERR_PTR(-ENOMEM);
	memory_object_inc(memory_object_snapimage);

	minor = ida_alloc_range(&snapimage_devt_ida, 0, NR_SNAPIMAGE_DEVT - 1,
				GFP_KERNEL);
	if (minor < 0) {
		ret = minor;
		pr_err("Failed to allocate minor for snapshot image device. errno=%d\n",
		       abs(ret));
		goto fail_free_image;
	}

	snapimage->is_ready = true;
	snapimage->capacity = cbt_map->device_capacity;
	snapimage->image_dev_id = MKDEV(_major, minor);
	pr_info("Create snapshot image device [%u:%u] for original device [%u:%u]\n",
		MAJOR(snapimage->image_dev_id),
		MINOR(snapimage->image_dev_id),
		MAJOR(diff_area->orig_bdev->bd_dev),
		MINOR(diff_area->orig_bdev->bd_dev));

	ret = snapimage_start_worker(snapimage);
	if (ret) {
		pr_err("Failed to start worker thread. errno=%d\n", abs(ret));
		goto fail_free_minor;
	}

	disk = blk_alloc_disk(NUMA_NO_NODE);
	if (!disk) {
		pr_err("Failed to allocate disk\n");
		ret = -ENOMEM;
		goto fail_free_worker;
	}
	snapimage->disk = disk;

	blk_queue_max_hw_sectors(disk->queue, BLK_DEF_MAX_SECTORS);
	blk_queue_flag_set(QUEUE_FLAG_NOMERGES, disk->queue);

	if (snprintf(disk->disk_name, DISK_NAME_LEN, "%s%d",
		     BLK_SNAP_IMAGE_NAME, minor) < 0) {
		pr_err("Unable to set disk name for snapshot image device: invalid minor %u\n",
		       minor);
		ret = -EINVAL;
		goto fail_cleanup_disk;
	}
	pr_debug("Snapshot image disk name [%s]\n", disk->disk_name);

	disk->flags = 0;
#ifdef GENHD_FL_NO_PART_SCAN
	disk->flags |= GENHD_FL_NO_PART_SCAN;
#else
	disk->flags |= GENHD_FL_NO_PART;
#endif
	disk->major = _major;
	disk->first_minor = minor;
	disk->minors = 1; /* One disk has only one partition */

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
	snapimage_stop_worker(snapimage);
fail_free_minor:
	ida_free(&snapimage_devt_ida, minor);
fail_free_image:
	kfree(snapimage);
	memory_object_dec(memory_object_snapimage);
	return ERR_PTR(ret);
}

int snapimage_init(void)
{
	int ret = 0;

	ret = register_blkdev(0, BLK_SNAP_IMAGE_NAME);
	if (ret < 0) {
		pr_err("Failed to register snapshot image block device\n");
		return ret;
	}

	_major = ret;
	pr_info("Snapshot image block device major %d was registered\n",
		_major);

	return 0;
}

void snapimage_done(void)
{

	unregister_blkdev(_major, BLK_SNAP_IMAGE_NAME);
	pr_info("Snapshot image block device [%d] was unregistered\n", _major);
}

int snapimage_major(void)
{
	return _major;
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
