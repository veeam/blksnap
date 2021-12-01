// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME "-tracker: " fmt
#include <linux/slab.h>
#include <linux/blk-mq.h>
#include <linux/sched/mm.h>

#include "params.h"
#include "blk_snap.h"
#include "tracker.h"
#include "cbt_map.h"
#include "diff_area.h"

#ifdef CONFIG_DEBUGLOG
#undef pr_debug
#define pr_debug(fmt, ...) \
	printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
#endif

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

struct tracked_device {
	struct list_head link;
	dev_t dev_id;
};

LIST_HEAD(tracked_device_list);
DEFINE_SPINLOCK(tracked_device_lock);

void tracker_free(struct kref *kref)
{
	struct tracker *tracker = container_of(kref, struct tracker, kref);

	pr_debug("Free tracker for device [%u:%u].\n",
		MAJOR(tracker->dev_id), MINOR(tracker->dev_id));

	diff_area_put(tracker->diff_area);
	cbt_map_put(tracker->cbt_map);
	kfree(tracker);
}

struct tracker *tracker_get_by_dev(struct block_device *bdev)
{
	struct tracker *tracker;

	bdev_filter_read_lock(bdev);

	tracker = bdev_filter_get_ctx(bdev, KBUILD_MODNAME);
	if (likely(tracker))
		kref_get(&tracker->kref);

	bdev_filter_read_unlock(bdev);
	return tracker;
}

static
bool tracker_submit_bio_cb(struct bio *bio, void *ctx)
{
	int err = 0;
	struct tracker *tracker = ctx;
	sector_t sector;
	sector_t count;

	if (!op_is_write(bio_op(bio)))
		return true;

	if (!bio->bi_iter.bi_size)
		return true;

	sector = bio->bi_iter.bi_sector;
	count = (sector_t)(round_up(bio->bi_iter.bi_size, SECTOR_SIZE) / SECTOR_SIZE);

	err = cbt_map_set(tracker->cbt_map, sector, count);
	if (unlikely(err))
		return true;

	if (!atomic_read(&tracker->snapshot_is_taken))
		return true;

	err = diff_area_copy(tracker->diff_area, sector, count,
			     (bool)(bio->bi_opf & REQ_NOWAIT));
	if (likely(!err))
		return true;

	if (err == -EAGAIN) {
		bio_wouldblock_error(bio);
		return false;
	}

	pr_err("Failed to copy data to diff storage with error %d\n", abs(err));
	return true;
}

static
void tracker_detach_cb(void *ctx)
{
	struct tracker *tracker = ctx;

	tracker_put(tracker);
}

static const
struct bdev_filter_operations tracker_fops = {
	.submit_bio_cb = tracker_submit_bio_cb,
	.detach_cb = tracker_detach_cb
};

enum filter_cmd {
	filter_cmd_add,
	filter_cmd_del
};

static
int tracker_filter(struct tracker *tracker, enum filter_cmd flt_cmd,
		     struct block_device* bdev)
{
	int ret;
	unsigned int current_flag;
#if defined(HAVE_SUPER_BLOCK_FREEZE)
	struct super_block *superblock = NULL;
#else
	bool is_frozen = false;
#endif

	pr_debug("Tracker %s filter\n",
		(flt_cmd == filter_cmd_add) ? "add" : "delete");

#if defined(HAVE_SUPER_BLOCK_FREEZE)
	_freeze_bdev(bdev, &superblock);
#else
	if (freeze_bdev(bdev))
		pr_err("Failed to freeze device [%u:%u]\n",
		       MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev));
	else {
		is_frozen = true;
		pr_debug("Device [%u:%u] was frozen\n",
			MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev));
	}
#endif

	current_flag = memalloc_noio_save();
	bdev_filter_write_lock(bdev);

	switch (flt_cmd) {
	case filter_cmd_add:
		ret = bdev_filter_add(bdev, KBUILD_MODNAME, &tracker_fops, tracker);
		break;
	case filter_cmd_del:
		ret = bdev_filter_del(bdev, KBUILD_MODNAME);
		break;
	default:
		ret = -EINVAL;
	}

	bdev_filter_write_unlock(bdev);
	memalloc_noio_restore(current_flag);

#if defined(HAVE_SUPER_BLOCK_FREEZE)
	_thaw_bdev(bdev, superblock);
#else
	if (is_frozen) {
		if (thaw_bdev(bdev))
			pr_err("Failed to thaw device [%u:%u]\n",
			       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
		else
			pr_debug("Device [%u:%u] was unfrozen\n",
				MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev));
	}
#endif

	if (ret)
		pr_err("Failed to %s device [%u:%u]\n",
		       (flt_cmd == filter_cmd_add) ? "attach tracker to" : "detach tracker from",
		       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
	return ret;
}

static
struct tracker *tracker_new(struct block_device* bdev)
{
	int ret;
	struct tracker *tracker = NULL;
	struct cbt_map *cbt_map;

	pr_debug("Creating tracker for device [%u:%u].\n",
		MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev));

	tracker = kzalloc(sizeof(struct tracker), GFP_KERNEL);
	if (tracker == NULL)
		return ERR_PTR(-ENOMEM);

	kref_init(&tracker->kref);
	atomic_set(&tracker->snapshot_is_taken, false);
	tracker->dev_id = bdev->bd_dev;

	pr_info("Create tracker for device [%u:%u]. Capacity 0x%llx sectors\n",
		MAJOR(tracker->dev_id), MINOR(tracker->dev_id),
		(unsigned long long)bdev_nr_sectors(bdev));

	cbt_map = cbt_map_create(bdev);
	if (!cbt_map) {
		pr_err("Failed to create tracker for device [%u:%u]\n",
		       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
		ret = -ENOMEM;
		goto fail;
	}
	tracker->cbt_map = cbt_map;

	ret = tracker_filter(tracker, filter_cmd_add, bdev);
	if (ret) {
		pr_err("Failed to attach tracker. errno=%d\n", abs(ret));
		goto fail;
	}
	/*
	 * The filter store a pointer to the tracker.
	 * The tracker will not be released until its filter is released.
	 */
	kref_get(&tracker->kref);

	pr_debug("New tracker for device [%u:%u] was created.\n",
		MAJOR(tracker->dev_id), MINOR(tracker->dev_id));

	return tracker;
fail:
	tracker_put(tracker);
	return ERR_PTR(ret);
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

	pr_debug("Tracker for device [%u:%u] release snapshot\n",
		MAJOR(tracker->dev_id), MINOR(tracker->dev_id));

	atomic_set(&tracker->snapshot_is_taken, false);
}

void tracker_done(void)
{
	struct tracked_device *tr_dev;

	pr_debug("Cleanup trackers\n");
	while(true) {
		spin_lock(&tracked_device_lock);
		tr_dev = list_first_entry_or_null(&tracked_device_list,
					 struct tracked_device, link);
		if (tr_dev)
			list_del(&tr_dev->link);
		spin_unlock(&tracked_device_lock);

		if (!tr_dev)
			break;

		tracker_remove(tr_dev->dev_id);
		kfree(tr_dev);
	}
}

struct tracker *tracker_create_or_get(dev_t dev_id)
{
	struct tracker *tracker;
	struct block_device *bdev;
	struct tracked_device *tr_dev;

	bdev = blkdev_get_by_dev(dev_id, 0, NULL);
	if (IS_ERR(bdev)) {
		pr_err("Cannot open device [%u:%u]\n",
			MAJOR(dev_id), MINOR(dev_id));
		return ERR_PTR(PTR_ERR(bdev));
	}

	tracker = tracker_get_by_dev(bdev);
	if (tracker){
		pr_debug("Device [%u:%u] is already under tracking\n",
			MAJOR(dev_id), MINOR(dev_id));
		goto put_bdev;
	}

	tr_dev = kzalloc(sizeof(struct tracked_device), GFP_KERNEL);
	if (!tr_dev) {
		tracker = ERR_PTR(-ENOMEM);
		goto put_bdev;
	}
	INIT_LIST_HEAD(&tr_dev->link);
	tr_dev->dev_id = dev_id;

	tracker = tracker_new(bdev);
	if (IS_ERR(tracker)) {
		pr_err("Failed to create tracker. errno=%d\n",
			abs((int)PTR_ERR(tracker)));
		kfree(tr_dev);
	} else {
		spin_lock(&tracked_device_lock);
		list_add_tail(&tr_dev->link, &tracked_device_list);
		spin_unlock(&tracked_device_lock);
	}
put_bdev:
	blkdev_put(bdev, 0);
	return tracker;
}

int tracker_remove(dev_t dev_id)
{
	int ret;
	struct tracker *tracker;
	struct block_device *bdev;

	pr_info("Removing device [%u:%u] from tracking\n",
		MAJOR(dev_id), MINOR(dev_id));

	bdev = blkdev_get_by_dev(dev_id, 0, NULL);
	if (IS_ERR(bdev)) {
		pr_err("Cannot open device [%u:%u]\n",
			MAJOR(dev_id), MINOR(dev_id));
		return PTR_ERR(bdev);
	}

	tracker = tracker_get_by_dev(bdev);
	if (!tracker) {
		pr_err("Unable to remove device [%u:%u] from tracking: ",
			MAJOR(dev_id), MINOR(dev_id));
		pr_err("tracker not found\n");
		ret = -ENODATA;
		goto put_bdev;
	}

	if (atomic_read(&tracker->snapshot_is_taken)) {
		pr_err("Tracker for device [%u:%u] is busy with a snapshot\n",
			MAJOR(dev_id), MINOR(dev_id));
		ret = -EBUSY;
		goto put_tracker;
	}

	ret = tracker_filter(tracker, filter_cmd_del, bdev);
	if (ret)
		pr_err("Failed to remove tracker from device [%u:%u]\n",
			MAJOR(dev_id), MINOR(dev_id));
	else {
		struct tracked_device *tr_dev = NULL;
		struct tracked_device *iter_tr_dev;

		spin_lock(&tracked_device_lock);
		list_for_each_entry(iter_tr_dev, &tracked_device_list, link) {
			if (iter_tr_dev->dev_id == dev_id) {
				list_del(&iter_tr_dev->link);
				tr_dev = iter_tr_dev;
				break;
			}
		}
		spin_unlock(&tracked_device_lock);

		kfree(tr_dev);
	}
put_tracker:
	tracker_put(tracker);
put_bdev:
	blkdev_put(bdev, 0);
	return ret;
}

int tracker_read_cbt_bitmap(dev_t dev_id, unsigned int offset, size_t length,
			     char __user *user_buff)
{
	int ret;
	struct tracker *tracker;
	struct block_device *bdev;

	bdev = blkdev_get_by_dev(dev_id, 0, NULL);
	if (IS_ERR(bdev)) {
		pr_err("Cannot open device [%u:%u]\n",
			MAJOR(dev_id), MINOR(dev_id));
		return PTR_ERR(bdev);
	}

	tracker = tracker_get_by_dev(bdev);
	if (!tracker) {
		pr_err("Unable to read CBT bitmap for device [%u:%u]: ",
			MAJOR(dev_id), MINOR(dev_id));
		pr_err("tracker not found\n");
		ret = -ENODATA;
		goto put_bdev;
	}

	if (!atomic_read(&tracker->snapshot_is_taken)) {
		pr_err("Unable to read CBT bitmap for device [%u:%u]: ",
			MAJOR(dev_id), MINOR(dev_id));
		pr_err("device is not captured by snapshot\n");
		ret = -EPERM;
		goto put_tracker;
	}

	ret = cbt_map_read_to_user(tracker->cbt_map, user_buff, offset, length);

put_tracker:
	tracker_put(tracker);
put_bdev:
	blkdev_put(bdev, 0);
	return ret;
}

static inline
void collect_cbt_info(dev_t dev_id, struct blk_snap_cbt_info *cbt_info)
{
	struct block_device *bdev;
	struct tracker *tracker;

	bdev = blkdev_get_by_dev(dev_id, 0, NULL);
	if (IS_ERR(bdev)) {
		pr_err("Cannot open device [%u:%u]\n",
			MAJOR(dev_id), MINOR(dev_id));
		return;
	}

	tracker = tracker_get_by_dev(bdev);
	if (!tracker || !tracker->cbt_map)
		goto put_bdev;

	cbt_info->device_capacity =
		(__u64)(tracker->cbt_map->device_capacity << SECTOR_SHIFT);
	cbt_info->blk_size = (__u32)cbt_map_blk_size(tracker->cbt_map);
	cbt_info->blk_count = (__u32)tracker->cbt_map->blk_count;
	cbt_info->snap_number = (__u8)tracker->cbt_map->snap_number_previous;
	uuid_copy(&cbt_info->generationId, &tracker->cbt_map->generationId);

put_bdev:
	blkdev_put(bdev, 0);
}

int tracker_collect(int max_count, struct blk_snap_cbt_info *cbt_info, int *pcount)
{
	int ret = 0;
	int count = 0;
	int iter = 0;
	struct tracked_device *tr_dev;

	if (!cbt_info) {
		/**
		 * Just calculate trackers list length
		 */
		spin_lock(&tracked_device_lock);
		list_for_each_entry(tr_dev, &tracked_device_list, link)
			++count;
		spin_unlock(&tracked_device_lock);
		goto out;
	}

	spin_lock(&tracked_device_lock);
	list_for_each_entry(tr_dev, &tracked_device_list, link) {
		if (count >= max_count) {
			ret = -ENOBUFS;
			break;
		}

		cbt_info[count].dev_id.mj = MAJOR(tr_dev->dev_id);
		cbt_info[count].dev_id.mn = MINOR(tr_dev->dev_id);
		++count;
	}
	spin_unlock(&tracked_device_lock);

	if (ret)
		return ret;

	for (iter = 0; iter < count; iter++){
		dev_t dev_id = MKDEV(cbt_info[iter].dev_id.mj,
				     cbt_info[iter].dev_id.mn);

		collect_cbt_info(dev_id, &cbt_info[iter]);
	}
out:
	*pcount = count;
	return 0;
}

int tracker_mark_dirty_blocks(dev_t dev_id, struct blk_snap_block_range *block_ranges,
			      unsigned int count)
{
	int ret = 0;
	size_t inx;
	struct tracker *tracker;
	struct block_device *bdev;

	bdev = blkdev_get_by_dev(dev_id, 0, NULL);
	if (IS_ERR(bdev)) {
		pr_err("Cannot open device [%u:%u]\n",
			MAJOR(dev_id), MINOR(dev_id));
		return PTR_ERR(bdev);
	}

	pr_debug("Marking [%d] dirty blocks for device [%u:%u]\n",
		count, MAJOR(dev_id), MINOR(dev_id));

	tracker = tracker_get_by_dev(bdev);
	if (!tracker) {
		pr_err("Cannot find device [%u:%u]\n",
			MAJOR(dev_id), MINOR(dev_id));
		ret = -ENODEV;
		goto put_bdev;
	}

	for (inx = 0; inx < count; inx++) {
		ret = cbt_map_set_both(tracker->cbt_map,
				       (sector_t)block_ranges[inx].sector_offset,
				       (sector_t)block_ranges[inx].sector_count);
		if (ret) {
			pr_err("Failed to set CBT table. errno=%d\n", abs(ret));
			break;
		}
	}

	tracker_put(tracker);
put_bdev:
	blkdev_put(bdev, 0);
	return ret;
}
