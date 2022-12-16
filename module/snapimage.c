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

#define SNAPIMAGE_MAX_DEVICES 2048

static unsigned int _major;
static DEFINE_IDR(_minor_idr);
static DEFINE_SPINLOCK(_minor_lock);

static void free_minor(int minor)
{
	spin_lock(&_minor_lock);
	idr_remove(&_minor_idr, minor);
	spin_unlock(&_minor_lock);
}

static int new_minor(int *minor, void *ptr)
{
	int ret;

	idr_preload(GFP_KERNEL);
	spin_lock(&_minor_lock);

	ret = idr_alloc(&_minor_idr, ptr, 0, 1 << MINORBITS, GFP_NOWAIT);

	spin_unlock(&_minor_lock);
	idr_preload_end();

	if (ret < 0)
		return ret;

	*minor = ret;
	return 0;
}

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

struct snapimage_cmd {
	struct kthread_work work;
};

static void snapimage_queue_work(struct kthread_work *work)
{
	struct snapimage_cmd *cmd =
		container_of(work, struct snapimage_cmd, work);
	struct request *rq = blk_mq_rq_from_pdu(cmd);
	struct snapimage *snapimage = rq->q->queuedata;
	blk_status_t status = BLK_STS_OK;
	struct bio_vec bvec;
	struct req_iterator iter;
	struct diff_area_image_ctx io_ctx;
	sector_t pos = blk_rq_pos(rq);

	diff_area_throttling_io(snapimage->diff_area);
	diff_area_image_ctx_init(&io_ctx, snapimage->diff_area,
				 op_is_write(req_op(rq)));
	rq_for_each_segment(bvec, rq, iter) {
		status = diff_area_image_io(&io_ctx, &bvec, &pos);
		if (unlikely(status != BLK_STS_OK))
			break;
	}
	diff_area_image_ctx_done(&io_ctx);

	blk_mq_end_request(rq, status);
}

static int snapimage_init_request(struct blk_mq_tag_set *set,
				  struct request *rq, unsigned int hctx_idx,
				  unsigned int numa_node)
{
	struct snapimage_cmd *cmd = blk_mq_rq_to_pdu(rq);

	kthread_init_work(&cmd->work, snapimage_queue_work);
	return 0;
}

/*
 * Cannot fall asleep in the context of this function, as we are under
 * rwsem lockdown.
 */
static blk_status_t snapimage_queue_rq(struct blk_mq_hw_ctx *hctx,
				       const struct blk_mq_queue_data *bd)
{
	int ret;
	struct request *rq = bd->rq;
	struct snapimage *snapimage = rq->q->queuedata;
	struct snapimage_cmd *cmd = blk_mq_rq_to_pdu(rq);

	blk_mq_start_request(rq);

	if (unlikely(!snapimage->is_ready || diff_area_is_corrupted(snapimage->diff_area))) {
		blk_mq_end_request(rq, BLK_STS_NOSPC);
		return BLK_STS_NOSPC;
	}

	if (op_is_write(req_op(rq))) {
		ret = cbt_map_set_both(snapimage->cbt_map, blk_rq_pos(rq),
				       blk_rq_sectors(rq));
		if (unlikely(ret)) {
			blk_mq_end_request(rq, BLK_STS_IOERR);
			return BLK_STS_IOERR;
		}
	}

	kthread_queue_work(&snapimage->worker, &cmd->work);
	return BLK_STS_OK;
}

static const struct blk_mq_ops mq_ops = {
	.queue_rq = snapimage_queue_rq,
	.init_request = snapimage_init_request,
};

const struct block_device_operations bd_ops = {
	.owner = THIS_MODULE,
};

static inline int snapimage_alloc_tag_set(struct snapimage *snapimage)
{
	struct blk_mq_tag_set *set = &snapimage->tag_set;

	set->ops = &mq_ops;
	set->nr_hw_queues = 1;
	set->nr_maps = 1;
	set->queue_depth = 128;
	set->numa_node = NUMA_NO_NODE;
	set->flags = BLK_MQ_F_SHOULD_MERGE | BLK_MQ_F_STACKING;

	set->cmd_size = sizeof(struct snapimage_cmd);
	set->driver_data = snapimage;

	return blk_mq_alloc_tag_set(set);
}

void snapimage_free(struct snapimage *snapimage)
{
	pr_info("Snapshot image disk [%u:%u] delete\n",
		MAJOR(snapimage->image_dev_id), MINOR(snapimage->image_dev_id));

	blk_mq_freeze_queue(snapimage->disk->queue);
	snapimage->is_ready = false;
	blk_mq_unfreeze_queue(snapimage->disk->queue);

	snapimage_unprepare_worker(snapimage);

	del_gendisk(snapimage->disk);
#ifdef HAVE_BLK_MQ_ALLOC_DISK
#ifdef HAVE_BLK_CLEANUP_DISK
	blk_cleanup_disk(snapimage->disk);
#else
	put_disk(snapimage->disk);
#endif
#else
	blk_cleanup_queue(snapimage->queue);
	put_disk(snapimage->disk);
#endif
	blk_mq_free_tag_set(&snapimage->tag_set);

	diff_area_put(snapimage->diff_area);
	cbt_map_put(snapimage->cbt_map);

	free_minor(MINOR(snapimage->image_dev_id));
	kfree(snapimage);
	memory_object_dec(memory_object_snapimage);
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

	snapimage = kzalloc(sizeof(struct snapimage), GFP_KERNEL);
	if (snapimage == NULL)
		return ERR_PTR(-ENOMEM);
	memory_object_inc(memory_object_snapimage);

	ret = new_minor(&minor, snapimage);
	if (ret) {
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

	ret = snapimage_alloc_tag_set(snapimage);
	if (ret) {
		pr_err("Failed to allocate tag set. errno=%d\n", abs(ret));
		goto fail_free_worker;
	}

#ifdef HAVE_BLK_MQ_ALLOC_DISK
	disk = blk_mq_alloc_disk(&snapimage->tag_set, snapimage);
	if (IS_ERR(disk)) {
		ret = PTR_ERR(disk);
		pr_err("Failed to allocate disk. errno=%d\n", abs(ret));
		goto fail_free_tagset;
	}
#else
	queue = blk_mq_init_queue(&snapimage->tag_set);
	if (IS_ERR(queue)) {
		ret = PTR_ERR(queue);
		pr_err("Failed to allocate queue. errno=%d\n", abs(ret));
		goto fail_free_tagset;
	}

	disk = alloc_disk(1);
	if (!disk) {
		pr_err("Failed to allocate disk\n");
		goto fail_free_queue;
	}
	disk->queue = queue;

	snapimage->queue = queue;
	snapimage->queue->queuedata = snapimage;

	blk_queue_bounce_limit(snapimage->queue, BLK_BOUNCE_HIGH);
#endif

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
	snapimage->disk = disk;

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
#ifdef HAVE_BLK_MQ_ALLOC_DISK
#ifdef HAVE_BLK_CLEANUP_DISK
	blk_cleanup_disk(disk);
#else
	put_disk(disk);
#endif
#else
	del_gendisk(disk);
fail_free_queue:
	blk_cleanup_queue(queue);
#endif
fail_free_tagset:
	blk_mq_free_tag_set(&snapimage->tag_set);
fail_free_worker:
	snapimage_unprepare_worker(snapimage);
fail_free_minor:
	free_minor(minor);
fail_free_image:
	kfree(snapimage);
	memory_object_dec(memory_object_snapimage);
	return ERR_PTR(ret);
}

int snapimage_init(void)
{
	int mj = 0;

	mj = register_blkdev(mj, BLK_SNAP_IMAGE_NAME);
	if (mj < 0) {
		pr_err("Failed to register snapshot image block device. errno=%d\n",
		       abs(mj));
		return mj;
	}
	_major = mj;
	pr_info("Snapshot image block device major %d was registered\n",
		_major);

	return 0;
}

void snapimage_done(void)
{
	unregister_blkdev(_major, BLK_SNAP_IMAGE_NAME);
	pr_info("Snapshot image block device [%d] was unregistered\n", _major);

	idr_destroy(&_minor_idr);
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
