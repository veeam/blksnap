// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Veeam Software Group GmbH */
#define pr_fmt(fmt) KBUILD_MODNAME "-tracker: " fmt

#include <linux/slab.h>
#include <linux/blk-mq.h>
#include <linux/sched/mm.h>
#include <linux/build_bug.h>
#include <linux/blk-crypto.h>
#ifdef BLKSNAP_STANDALONE
#include "veeamblksnap.h"
#include "bdevfilter-internal.h"
#include "compat.h"
#else
#include <uapi/linux/blksnap.h>
#endif
#include "tracker.h"
#include "cbt_map.h"
#include "diff_area.h"
#include "snapimage.h"
#include "snapshot.h"

void tracker_free(struct kref *kref)
{
	struct tracker *tracker = container_of(kref, struct tracker, kref);

	might_sleep();

	pr_debug("Free tracker for device [%u:%u]\n", MAJOR(tracker->dev_id),
		 MINOR(tracker->dev_id));

	if (tracker->diff_area)
		diff_area_put(tracker->diff_area);
	if (tracker->cbt_map)
		cbt_map_destroy(tracker->cbt_map);

	kfree(tracker);
}
#ifdef BLKSNAP_STANDALONE
static bool tracker_submit_bio(struct bio *bio, struct blkfilter *flt)
{
#else
static bool tracker_submit_bio(struct bio *bio)
{
	struct blkfilter *flt = bio->bi_bdev->bd_filter;
#endif
	struct tracker *tracker = container_of(flt, struct tracker, filter);
	sector_t count = bio_sectors(bio);
	sector_t sector = bio->bi_iter.bi_sector;

#if !defined(BLKSNAP_STANDALONE)
	if (WARN_ON_ONCE(current->blk_filter != flt))
		return false;
#endif
	if (!op_is_write(bio_op(bio)) || !count)
		return false;
#if !defined(BLKSNAP_STANDALONE)
	if (bio_flagged(bio, BIO_REMAPPED))
		sector -= bio->bi_bdev->bd_start_sect;
#endif
	if (cbt_map_set(tracker->cbt_map, sector, count))
		return false;

	if (!atomic_read(&tracker->snapshot_is_taken))
		return false;
	/*
	 * The diff_area is not blocked from releasing now, because
	 * changing the value of the snapshot_is_taken is performed when
	 * the block device queue is frozen in tracker_release_snapshot().
	 */
	if (diff_area_is_corrupted(tracker->diff_area))
		return false;

#ifdef CONFIG_BLK_INLINE_ENCRYPTION
	if (bio_has_crypt_ctx(bio)) {
		pr_err("Inline encryption is not supported\n");
		diff_area_set_corrupted(tracker->diff_area, -EPERM);
		return false;
	}
#endif
#ifdef CONFIG_BLK_DEV_INTEGRITY
	if (bio->bi_integrity) {
		pr_err("Data integrity is not supported\n");
		diff_area_set_corrupted(tracker->diff_area, -EPERM);
		return false;
	}
#endif
	return diff_area_cow(tracker->diff_area, bio);
}

static struct blkfilter *tracker_attach(struct block_device *bdev)
{
	struct tracker *tracker = NULL;
	struct cbt_map *cbt_map;

	pr_debug("Creating tracker for device [%u:%u]\n",
		 MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev));

	cbt_map = cbt_map_create(bdev);
	if (!cbt_map) {
		pr_err("Failed to create CBT map for device [%u:%u]\n",
		       MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev));
		return ERR_PTR(-ENOMEM);
	}

	tracker = kzalloc(sizeof(struct tracker), GFP_KERNEL);
	if (tracker == NULL) {
		cbt_map_destroy(cbt_map);
		return ERR_PTR(-ENOMEM);
	}

	tracker->orig_bdev = bdev;
	mutex_init(&tracker->ctl_lock);
	INIT_LIST_HEAD(&tracker->link);
	kref_init(&tracker->kref);
	tracker->dev_id = bdev->bd_dev;
	atomic_set(&tracker->snapshot_is_taken, false);
	tracker->cbt_map = cbt_map;
	tracker->diff_area = NULL;

	pr_debug("New tracker for device [%u:%u] was created\n",
		 MAJOR(tracker->dev_id), MINOR(tracker->dev_id));

	return &tracker->filter;
}

static void tracker_detach(struct blkfilter *flt)
{
	struct tracker *tracker = container_of(flt, struct tracker, filter);

	pr_debug("Detach tracker from device [%u:%u]\n",
		 MAJOR(tracker->dev_id), MINOR(tracker->dev_id));

	tracker_put(tracker);
}

static int ctl_cbtinfo(struct tracker *tracker, __u8 __user *buf, __u32 *plen)
{
	struct cbt_map *cbt_map = tracker->cbt_map;
	struct blksnap_cbtinfo arg;

	if (!cbt_map)
		return -ESRCH;

	if (*plen < sizeof(arg))
		return -EINVAL;

	arg.device_capacity = (__u64)(cbt_map->device_capacity << SECTOR_SHIFT);
	arg.block_size = (__u32)(1 << cbt_map->blk_size_shift);
	arg.block_count = (__u32)cbt_map->blk_count;
	export_uuid(arg.generation_id.b, &cbt_map->generation_id);
	arg.changes_number = (__u8)cbt_map->snap_number_previous;

	if (copy_to_user(buf, &arg, sizeof(arg)))
		return -ENODATA;

	*plen = sizeof(arg);
	return 0;
}

static int ctl_cbtmap(struct tracker *tracker, __u8 __user *buf, __u32 *plen)
{
	struct cbt_map *cbt_map = tracker->cbt_map;
	struct blksnap_cbtmap arg;

	if (!cbt_map)
		return -ESRCH;

	if (unlikely(cbt_map->is_corrupted)) {
		pr_err("CBT table was corrupted\n");
		return -EFAULT;
	}

	if (*plen < sizeof(arg))
		return -EINVAL;

	if (copy_from_user(&arg, buf, sizeof(arg)))
		return -ENODATA;

	if (arg.length > (cbt_map->blk_count - arg.offset))
		return -ENODATA;

	if (copy_to_user(u64_to_user_ptr(arg.buffer),
			 cbt_map->read_map + arg.offset, arg.length))

		return -EINVAL;

	*plen = 0;
	return 0;
}

static int ctl_cbtdirty(struct tracker *tracker, __u8 __user *buf, __u32 *plen)
{
	struct cbt_map *cbt_map = tracker->cbt_map;
	struct blksnap_cbtdirty arg;
	unsigned int inx;

	if (!cbt_map)
		return -ESRCH;

	if (*plen < sizeof(arg))
		return -EINVAL;

	if (copy_from_user(&arg, buf, sizeof(arg)))
		return -ENODATA;

	for (inx = 0; inx < arg.count; inx++) {
		struct blksnap_sectors range;
		int ret;

		if (copy_from_user(&range, u64_to_user_ptr(arg.dirty_sectors),
				   sizeof(range)))
			return -ENODATA;

		ret = cbt_map_set_both(cbt_map, range.offset, range.count);
		if (ret)
			return ret;
	}
	*plen = 0;
	return 0;
}

static int ctl_snapshotadd(struct tracker *tracker,
			   __u8 __user *buf, __u32 *plen)
{
	struct blksnap_snapshotadd arg;

	if (*plen < sizeof(arg))
		return -EINVAL;

	if (copy_from_user(&arg, buf, sizeof(arg)))
		return -ENODATA;

	*plen = 0;
	return  snapshot_add_device((uuid_t *)&arg.id, tracker);
}
static int ctl_snapshotinfo(struct tracker *tracker,
			    __u8 __user *buf, __u32 *plen)
{
	struct blksnap_snapshotinfo arg = {0};

	if (*plen < sizeof(arg))
		return -EINVAL;

	if (copy_from_user(&arg, buf, sizeof(arg)))
		return -ENODATA;

	if (tracker->diff_area && diff_area_is_corrupted(tracker->diff_area))
		arg.error_code = tracker->diff_area->error_code;
	else
		arg.error_code = 0;

	if (tracker->snap_disk)
		strscpy(arg.image, tracker->snap_disk->disk_name,
			IMAGE_DISK_NAME_LEN);

	if (copy_to_user(buf, &arg, sizeof(arg)))
		return -ENODATA;

	*plen = sizeof(arg);
	return 0;
}

static int tracker_ctl(struct blkfilter *flt, const unsigned int cmd,
		       __u8 __user *buf, __u32 *plen)
{
	int ret = 0;
	struct tracker *tracker = container_of(flt, struct tracker, filter);

	mutex_lock(&tracker->ctl_lock);
	switch (cmd) {
	case BLKFILTER_CTL_BLKSNAP_CBTINFO:
		ret = ctl_cbtinfo(tracker, buf, plen);
		break;
	case BLKFILTER_CTL_BLKSNAP_CBTMAP:
		ret = ctl_cbtmap(tracker, buf, plen);
		break;
	case BLKFILTER_CTL_BLKSNAP_CBTDIRTY:
		ret = ctl_cbtdirty(tracker, buf, plen);
		break;
	case BLKFILTER_CTL_BLKSNAP_SNAPSHOTADD:
		ret = ctl_snapshotadd(tracker, buf, plen);
		break;
	case BLKFILTER_CTL_BLKSNAP_SNAPSHOTINFO:
		ret = ctl_snapshotinfo(tracker, buf, plen);
		break;
	default:
		ret = -ENOTTY;
	};
	mutex_unlock(&tracker->ctl_lock);

	return ret;
}

#ifdef BLKSNAP_STANDALONE
static struct bdevfilter_operations tracker_ops = {
#else
static struct blkfilter_operations tracker_ops = {
#endif
	.owner		= THIS_MODULE,
	.name		= "blksnap",
	.attach		= tracker_attach,
	.detach		= tracker_detach,
	.ctl		= tracker_ctl,
	.submit_bio	= tracker_submit_bio,
};

int tracker_take_snapshot(struct tracker *tracker)
{
	int ret = 0;
	bool cbt_reset_needed = false;
	struct block_device *orig_bdev = tracker->orig_bdev;
	sector_t capacity;

#if defined(HAVE_SUPER_BLOCK_FREEZE)
	blk_mq_freeze_queue(orig_bdev->bd_disk->queue);
#else
	blk_mq_freeze_queue(orig_bdev->bd_queue);
#endif
	if (tracker->cbt_map->is_corrupted) {
		cbt_reset_needed = true;
		pr_warn("Corrupted CBT table detected. CBT fault\n");
	}

	capacity = bdev_nr_sectors(orig_bdev);
	if (tracker->cbt_map->device_capacity != capacity) {
		cbt_reset_needed = true;
		pr_warn("Device resize detected. CBT fault\n");
	}

	if (cbt_reset_needed) {
		ret = cbt_map_reset(tracker->cbt_map, capacity);
		if (ret) {
			pr_err("Failed to create tracker. errno=%d\n",
			       abs(ret));
			return ret;
		}
	}

	cbt_map_switch(tracker->cbt_map);
	atomic_set(&tracker->snapshot_is_taken, true);

#if defined(HAVE_SUPER_BLOCK_FREEZE)
	blk_mq_unfreeze_queue(orig_bdev->bd_disk->queue);
#else
	blk_mq_unfreeze_queue(orig_bdev->bd_queue);
#endif
	return 0;
}

void tracker_release_snapshot(struct tracker *tracker)
{
	struct diff_area *diff_area = tracker->diff_area;

	if (unlikely(!diff_area))
		return;

	snapimage_free(tracker);
#if defined(HAVE_SUPER_BLOCK_FREEZE)
	blk_mq_freeze_queue(tracker->orig_bdev->bd_disk->queue);
#else
	blk_mq_freeze_queue(tracker->orig_bdev->bd_queue);
#endif
	pr_debug("Tracker for device [%u:%u] release snapshot\n",
		 MAJOR(tracker->dev_id), MINOR(tracker->dev_id));

	atomic_set(&tracker->snapshot_is_taken, false);
	tracker->diff_area = NULL;
#if defined(HAVE_SUPER_BLOCK_FREEZE)
	blk_mq_unfreeze_queue(tracker->orig_bdev->bd_disk->queue);
#else
	blk_mq_unfreeze_queue(tracker->orig_bdev->bd_queue);
#endif
#ifdef CONFIG_BLKSNAP_COW_SCHEDULE
	flush_work(&diff_area->cow_queue_work);
#endif
	flush_work(&diff_area->image_io_work);
	flush_work(&diff_area->store_queue_work);

	diff_area_put(diff_area);
}

int __init tracker_init(void)
{
	pr_debug("Register filter '%s'", tracker_ops.name);
#ifdef BLKSNAP_STANDALONE
	return bdevfilter_register(&tracker_ops);
#else
	return blkfilter_register(&tracker_ops);
#endif
}

void tracker_done(void)
{
	pr_debug("Unregister filter '%s'", tracker_ops.name);
#ifdef BLKSNAP_STANDALONE
	bdevfilter_unregister(&tracker_ops);
#else
	blkfilter_unregister(&tracker_ops);
#endif
}
