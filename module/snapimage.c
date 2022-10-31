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

struct snapimage_bio_prefix
{
	struct kthread_work work;
	struct bio bio[0];
};

static unsigned int _major;
static DEFINE_IDA(snapimage_devt_ida);

struct bio_set snapimage_bioset;

static inline void snapimage_unprepare_worker(struct snapimage *snapimage)
{
	kthread_flush_worker(&snapimage->worker);
	kthread_stop(snapimage->worker_task);
}

static int snapimage_kthread_worker_fn(void *worker_ptr)
{
	current->flags |= PF_LOCAL_THROTTLE | PF_MEMALLOC_NOIO;
	return kthread_worker_fn(worker_ptr);
}

static inline int snapimage_prepare_worker(struct snapimage *snapimage)
{
	struct task_struct *task;

	kthread_init_worker(&snapimage->worker);

	task = kthread_run(snapimage_kthread_worker_fn, &snapimage->worker,
			   BLK_SNAP_IMAGE_NAME "%d",
			   MINOR(snapimage->image_dev_id));
	if (IS_ERR(task))
		return -ENOMEM;

	set_user_nice(task, MIN_NICE);

	snapimage->worker_task = task;
	return 0;
}

static void snapimage_process_bio(struct kthread_work *work)
{
	struct snapimage_bio_prefix *pfx =
		container_of(work, struct snapimage_bio_prefix, work);
	struct bio *bio = pfx->bio;
#ifdef HAVE_BI_BDEV
	struct snapimage *snapimage = bio->bi_bdev->bd_disk->private_data;
#endif
#ifdef HAVE_BI_BDISK
	struct snapimage *snapimage = bio->bi_disk->private_data;
#endif
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
	struct snapimage_bio_prefix *pfx =
		container_of(bio, struct snapimage_bio_prefix, bio[0]);

	kthread_init_work(&pfx->work, snapimage_process_bio);
	kthread_queue_work(&snapimage->worker, &pfx->work);

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

	snapimage_unprepare_worker(snapimage);

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

	ret = snapimage_prepare_worker(snapimage);
	if (ret) {
		pr_err("Failed to prepare worker thread. errno=%d\n", abs(ret));
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
	snapimage_unprepare_worker(snapimage);
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

	ret = bioset_init(&snapimage_bioset, 64,
			  sizeof(struct snapimage_bio_prefix),
			  BIOSET_NEED_BVECS | BIOSET_NEED_RESCUER);
	if (ret) {
		pr_err("Failed to initialize bioset\n");
		unregister_blkdev(_major, BLK_SNAP_IMAGE_NAME);
	}

	return ret;
}

void snapimage_done(void)
{
	bioset_exit(&snapimage_bioset);

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
