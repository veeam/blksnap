// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME "-tracker: " fmt
#include <linux/slab.h>
#include <linux/blk-mq.h>

#include "params.h"
#include "blk_snap.h"
#include "tracker.h"
#include "cbt_map.h"
#include "diff_area.h"

#ifdef HAVE_LP_FILTER
#include "lp_filter.h"
#endif

#ifndef HAVE_BDEV_NR_SECTORS
static inline
sector_t bdev_nr_sectors(struct block_device *bdev)
{
	return i_size_read(bdev->bd_inode) >> 9;
};
#endif

LIST_HEAD(trackers);
DEFINE_RWLOCK(trackers_lock);

void tracker_free(struct kref *kref)
{
	struct tracker *tracker = container_of(kref, struct tracker, kref);

	if (tracker->diff_area) {
		diff_area_put(tracker->diff_area);
		tracker->diff_area = NULL;
	}

	if (tracker->cbt_map) {
		cbt_map_put(tracker->cbt_map);
		tracker->cbt_map = NULL;
	}

	kfree(tracker);
}

struct tracker *tracker_get_by_dev_id(dev_t dev_id)
{
	struct tracker *result = NULL;
	struct tracker *tracker;

	read_lock(&trackers_lock);

	if (list_empty(&trackers))
		goto out;

	list_for_each_entry(tracker, &trackers, link) {
		if (tracker->dev_id == dev_id) {
			kref_get(&tracker->kref);
			result = tracker;
			break;
		}
	}
out:
	read_unlock(&trackers_lock);
	return result;
}

static
int tracker_submit_bio_cb(struct bio *bio, void *ctx)
{
	int err = 0;
	struct tracker *tracker = ctx;
	sector_t sector = bio->bi_iter.bi_sector;
	sector_t count = (sector_t)(bio->bi_iter.bi_size >> SECTOR_SHIFT);

	if (!op_is_write(bio_op(bio)))
		return FLT_ST_PASS;

	err = cbt_map_set(tracker->cbt_map, sector, count);
	if (unlikely(err))
		return FLT_ST_PASS;

	if (!atomic_read(&tracker->snapshot_is_taken))
		return FLT_ST_PASS;

	err = diff_area_copy(tracker->diff_area, sector, count,
	                     (bool)(bio->bi_opf & REQ_NOWAIT));
	if (likely(!err))
		return FLT_ST_PASS;

	if (err == -EAGAIN) {
		bio_wouldblock_error(bio);
		return FLT_ST_COMPLETE;
	}

	pr_err("Failed to copy data to diff storage with error %d\n", err);
	return FLT_ST_PASS;
}

static
void tracker_detach_cb(void *ctx)
{
	struct tracker *tracker = (struct tracker *)ctx;

	write_lock(&trackers_lock);
	list_del(&tracker->link);
	write_unlock(&trackers_lock);

	tracker_put(tracker);
}

static const
struct filter_operations tracker_fops = {
	.submit_bio_cb = tracker_submit_bio_cb,
	.detach_cb = tracker_detach_cb
};

enum filter_cmd {
	filter_cmd_add,
	filter_cmd_del
};

static
int tracker_filter(struct tracker *tracker, enum filter_cmd flt_cmd)
{
	int ret;
	struct block_device* bdev;
#if defined(HAVE_SUPER_BLOCK_FREEZE)
	struct super_block *superblock = NULL;
#endif

	bdev = blkdev_get_by_dev(tracker->dev_id, 0, NULL);
	if (IS_ERR(bdev)) {
		pr_err("Failed to open device [%u:%u]\n",
		       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
		return PTR_ERR(bdev);
	}

#if defined(HAVE_SUPER_BLOCK_FREEZE)
	_freeze_bdev(bdev, &superblock);
#else
	if (freeze_bdev(bdev))
		pr_err("Failed to freeze device [%u:%u]\n",
		       MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev));
#endif

	switch (flt_cmd) {
	case filter_cmd_add:
		ret = filter_add(bdev, &tracker_fops, tracker);
		break;
	case filter_cmd_del:
		ret = filter_del(bdev);
		break;
	default:
		ret = -EINVAL;
	}

#if defined(HAVE_SUPER_BLOCK_FREEZE)
	_thaw_bdev(bdev, superblock);
#else
	if (thaw_bdev(bdev))
		pr_err("Failed to thaw device [%u:%u]\n",
		       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
#endif

	blkdev_put(bdev, 0);
	return ret;
}

static
struct tracker *tracker_new(dev_t dev_id)
{
	int ret;
	struct tracker *tracker = NULL;
	struct block_device* bdev;

	tracker = kzalloc(sizeof(struct tracker), GFP_KERNEL);
	if (tracker == NULL)
		return ERR_PTR(-ENOMEM);

	kref_init(&tracker->kref);
	atomic_set(&tracker->snapshot_is_taken, false);
	tracker->dev_id = dev_id;

	bdev = blkdev_get_by_dev(dev_id, 0, NULL);
	if (IS_ERR(bdev)) {
		kfree(tracker);
		return ERR_PTR(PTR_ERR(bdev));
	}

	pr_info("Create tracker for device [%u:%u]. Capacity 0x%llx sectors\n",
		MAJOR(tracker->dev_id), MINOR(tracker->dev_id),
		(unsigned long long)bdev_nr_sectors(bdev));

	tracker->cbt_map = cbt_map_create(bdev);
	if (!tracker->cbt_map) {
		pr_err("Failed to create tracker for device [%u:%u]\n",
		       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
		tracker_put(tracker);
		return ERR_PTR(-ENOMEM);
	}

	ret = tracker_filter(tracker, filter_cmd_add);
	if (ret) {
		pr_err("Failed to attach tracker. errno=%d\n", abs(ret));
		tracker_put(tracker);
		return ERR_PTR(ret);
	}

	write_lock(&trackers_lock);
	list_add_tail(&tracker->link, &trackers);
	write_unlock(&trackers_lock);

	blkdev_put(bdev, 0);

	return tracker;
}

int tracker_take_snapshot(struct tracker *tracker)
{
	int ret = 0;
	bool cbt_reset_needed = false;
	sector_t capacity;

	if (tracker->cbt_map->is_corrupted) {
		cbt_reset_needed = true;
		pr_warn("Corrupted CBT table detected. CBT fault\n");
	}

	capacity = bdev_nr_sectors(tracker->diff_area->orig_bdev);
	if (tracker->cbt_map->device_capacity != capacity) {
		cbt_reset_needed = true;
		pr_warn("Device resize detected. CBT fault\n");
	}

	if (cbt_reset_needed) {
		ret = cbt_map_reset(tracker->cbt_map, capacity);
		if (ret) {
			pr_err("Failed to create tracker. errno=%d\n", abs(ret));
			return ret;
		}
	}

	cbt_map_switch(tracker->cbt_map);
	atomic_set(&tracker->snapshot_is_taken, true);

	return 0;
}

void tracker_release_snapshot(struct tracker *tracker)
{
	if (!tracker)
		return;

	if (!!atomic_read(&tracker->snapshot_is_taken))
		return;

	atomic_set(&tracker->snapshot_is_taken, false);
}

int tracker_init(void)
{
	return filter_enable();
}

void tracker_done(void)
{
	struct tracker *tracker;
	dev_t dev_id;
	struct block_device *bdev;
	int ret;

	write_lock(&trackers_lock);
	while ((tracker = list_first_entry_or_null(&trackers, struct tracker, link))) {
		dev_id = tracker->dev_id;
		write_unlock(&trackers_lock);

		bdev = blkdev_get_by_dev(dev_id, 0, NULL);
		if (IS_ERR(bdev)) {
			ret = PTR_ERR(bdev);
			pr_err("Cannot open device [%u:%u], errno=%d\n",
			       MAJOR(dev_id), MINOR(dev_id), abs(ret));
		} else {
			ret = filter_del(bdev);
			if (ret)
				pr_err("Failed to detach filter from  device [%u:%u], errno=%d\n",
			       		MAJOR(dev_id), MINOR(dev_id),
			       		abs(ret));
			blkdev_put(bdev, 0);
		}

		write_lock(&trackers_lock);
	}
	write_unlock(&trackers_lock);

}

struct tracker *tracker_create_or_get(dev_t dev_id)
{
	struct tracker *tracker;

	tracker = tracker_get_by_dev_id(dev_id);
	if (tracker){
		pr_info("Device [%u:%u] is already under tracking\n",
		        MAJOR(dev_id), MINOR(dev_id));
		return tracker;
	}

	pr_info("Create tracker for device [%u:%u]\n",
		MAJOR(dev_id), MINOR(dev_id));

	tracker = tracker_new(dev_id);
	if (IS_ERR(tracker)) {
		int ret = PTR_ERR(tracker);

		pr_err("Failed to create tracker. errno=%d\n",
			abs(ret));
	}

	return tracker;
}

int tracker_remove(dev_t dev_id)
{
	int ret;
	struct tracker *tracker = NULL;

	pr_info("Removing device [%u:%u] from tracking\n",
		MAJOR(dev_id), MINOR(dev_id));
	tracker = tracker_get_by_dev_id(dev_id);
	if (!tracker) {
		pr_err("Unable to remove device [%u:%u] from tracking: ",
			MAJOR(dev_id), MINOR(dev_id));
		pr_err("tracker not found\n");
		return -ENODATA;
	}

	if (atomic_read(&tracker->snapshot_is_taken)) {
		pr_info("Tracker for device [%u:%u] is busy with a snapshot\n",
			MAJOR(dev_id), MINOR(dev_id));
		return -EBUSY;
	}

	ret = tracker_filter(tracker, filter_cmd_del);
	if (ret)
		pr_err("Failed to remove tracker from device [%u:%u]\n",
			MAJOR(dev_id), MINOR(dev_id));

	tracker_put(tracker);
	return ret;
}

int tracker_read_cbt_bitmap(dev_t dev_id, unsigned int offset, size_t length,
			     char __user *user_buff)
{
	int ret;
	struct tracker *tracker;

	tracker = tracker_get_by_dev_id(dev_id);
	if (!tracker) {
		pr_err("Unable to read CBT bitmap for device [%u:%u]: ",
			MAJOR(dev_id), MINOR(dev_id));
		pr_err("tracker not found\n");
		return -ENODATA;
	}

	if (atomic_read(&tracker->snapshot_is_taken)) {
		ret = cbt_map_read_to_user(tracker->cbt_map, user_buff,
					   offset, length);
	} else {
		pr_err("Unable to read CBT bitmap for device [%u:%u]: ",
			MAJOR(dev_id), MINOR(dev_id));
		pr_err("device is not captured by snapshot\n");
		ret = -EPERM;
	}

	tracker_put(tracker);
	return ret;
}

int tracker_collect(int max_count, struct blk_snap_cbt_info *cbt_info, int *pcount)
{
	int ret = 0;
	int count = 0;
	struct tracker *tracker;

	read_lock(&trackers_lock);
	if (list_empty(&trackers)) {
		ret = -ENODATA;
		goto out;
	}

	if (!cbt_info) {
		/**
		 * Just calculate trackers list length
		 */
		list_for_each_entry(tracker, &trackers, link) {
			++count;
		}
		goto out;
	}

	list_for_each_entry(tracker, &trackers, link) {
		if (count >= max_count) {
			ret = -ENOBUFS;
			break;
		}

		cbt_info[count].dev_id.mj = MAJOR(tracker->dev_id);
		cbt_info[count].dev_id.mn = MINOR(tracker->dev_id);
		if (tracker->cbt_map) {
			cbt_info[count].device_capacity =
				(__u64)(tracker->cbt_map->device_capacity << SECTOR_SHIFT);
			cbt_info[count].blk_size = (__u32)cbt_map_blk_size(tracker->cbt_map);
			cbt_info[count].blk_count = (__u32)tracker->cbt_map->blk_count;
			cbt_info[count].snap_number = (__u8)tracker->cbt_map->snap_number_previous;
			uuid_copy((uuid_t *)(cbt_info[count].generationId),
				  &tracker->cbt_map->generationId);
		} else {
			cbt_info[count].device_capacity = 0;
			cbt_info[count].blk_size = 0;
			cbt_info[count].blk_count = 0;
			cbt_info[count].snap_number = 0;
		}

		++count;
	}
out:
	read_unlock(&trackers_lock);

	*pcount = count;
	return ret;
}

int tracker_mark_dirty_blocks(dev_t dev_id, struct blk_snap_block_range *block_ranges,
			      unsigned int count)
{
	struct tracker *tracker;
	size_t inx;
	int ret = 0;

	pr_info("Marking [%d] dirty blocks for device [%u:%u]\n",
		count, MAJOR(dev_id), MINOR(dev_id));

	tracker = tracker_get_by_dev_id(dev_id);
	if (tracker == NULL) {
		pr_err("Cannot find device [%u:%u]\n",
			MAJOR(dev_id), MINOR(dev_id));
		return -ENODEV;
	}

	for (inx = 0; inx < count; inx++) {
		ret = cbt_map_set_both(tracker->cbt_map,
				       (sector_t)block_ranges[inx].sector_offset,
				       (sector_t)block_ranges[inx].sector_count);
		if (ret != 0) {
			pr_err("Failed to set CBT table. errno=%d\n", abs(ret));
			break;
		}
	}
	tracker_put(tracker);

	return ret;
}
