// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-tracker"
#include "common.h"
#include "tracker.h"
#include "blk_util.h"
#include "params.h"

LIST_HEAD(trackers);
DEFINE_RWLOCK(trackers_lock);

void tracker_free(struct kref *kref)
{
	struct tracker *tracker = container_of(kref, struct tracker, kref);

	if (tracker->snapdev) {
		snapstore_device_put_resource(tracker->snapdev);
		tracker->snapdev = NULL;
	}

	if (tracker->cbt_map) {
		cbt_map_put_resource(tracker->cbt_map);
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
	
	list_for_each_entry(tracker, &trackers, list) {
		if (tracker->dev_id == dev_id) {
			tracker_get(tracker);
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

	if (!atomic_read(tracker->snapshot_is_taken))
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
	tracker_put((struct tracker *)ctx);
}

const
struct filter_operations *tracker_fops = {
	.submit_bio_cb = tracker_submit_bio_cb;
	.detach_cb = tracker_detach_cb;
}

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
		pr_err("Failed to open device [%d:%d]\n",
		       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
		return PTR_ERR(bdev);
	}

#if defined(HAVE_SUPER_BLOCK_FREEZE)
	_freeze_bdev(bdev, psuperblock);
#else
	if (freeze_bdev(bdev))
		pr_err("Failed to freeze device [%d:%d]\n",
		       MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev));
#endif

	switch (flt_cmd) {
	case filter_cmd_add:
		ret = filter_add(bdev, tracker_fops, tracker);
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
		pr_err("Failed to thaw device [%d:%d]\n",
		       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
#endif

	blkdev_put(bdev, 0);
	return ret;
}

static
int tracker_new(dev_t dev_id, unsigned long long snapshot_id)
{
	int ret;
	struct tracker *tracker = NULL;
	struct block_device* bdev;
	unsigned int blk_size_shift;
	sector_t capacity;

	tracker = kzalloc(sizeof(struct tracker), GFP_KERNEL);
	if (tracker == NULL)
		return = -ENOMEM;

	kref_init(&tracker->kref);
	atomic_set(&tracker->snapshot_is_taken, false);
	tracker->dev_d = dev_id;
	tracker->snapshot_id = 0;

	bdev = blkdev_get_by_dev(dev_id, 0, NULL);
	if (IS_ERR(bdev)) {
		kfree(tracker);
		return PTR_ERR(bdev);
	}

	pr_info("Create tracker for device [%d:%d]. Capacity 0x%llx sectors\n",
		MAJOR(tracker->dev_id), MINOR(tracker->dev_id),
		(unsigned long long)bdev_nr_sectors(bdev));

	tracker->cbt_map = cbt_map_create(bdev);
	if (tracker->cbt_map == NULL) {
		pr_err("Failed to create tracker for device [%d:%d]\n",
		       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
		ret = -ENOMEM;
	}

	if (ret) {
		tracker_put(tracker);
		*ptracker = NULL;
	} else
		*ptracker = tracker;

	blkdev_put(bdev, 0);

	return ret;
}

int tracker_take_snapshot(struct tracker *tracker)
{
	int ret = 0;
	bool cbt_reset_needed = false;

	if (tracker->cbt_map->is_corrupted) {
		cbt_reset_needed = true;
		pr_warn("Corrupted CBT table detected. CBT fault\n");
	}

	if (tracker->cbt_map->device_capacity != bdev_nr_sectors(tracker->diff_area->orig_bdev)) {
		cbt_reset_needed = true;
		pr_warn("Device resize detected. CBT fault\n");
	}

	if (cbt_reset_needed) {
		ret = cbt_map_reset(tracker->cbt_map);
		if (ret) {
			pr_err("Failed to create tracker. errno=%d\n", ret);
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
	filter_enable();
}

void tracker_done(void)
{
	write_lock(&trackers_lock);
	while (!list_empty(&trackers)) {
		struct tracker *tracker;

		tracker = list_first_entry(&trackers, struct tracker, list);
		bdev = blkdev_get_by_dev(tracker->dev_id, 0, NULL);
		if (!IS_ERR(bdev)) {
			int ret;

			ret = filter_del(bdev);
			if (ret)
				pr_err("Failed to detach filter from  device [%d:%d], errno=%d\n",
			       		MAJOR(tracker->dev_id), MINOR(tracker->dev_id),
			       		PTR_ERR(ret));
			blkdev_put(bdev, 0);
		} else
			pr_err("Cannot open device [%d:%d], errno=%d\n",
			       MAJOR(tracker->dev_id), MINOR(tracker->dev_id),
			       PTR_ERR(bdev));
		
		list_del(&tracker->list);
		tracker_put(tracker);
	}
	write_unlock(&trackers_lock);
}

struct tracker * tracker_create_or_get(dev_t dev_id)
{
	int result;
	struct tracker *tracker;

	tracker = tracker_get_by_dev_id(dev_id);
	if (tracker){
		pr_info("Device [%d:%d] is already under tracking\n",
		        MAJOR(dev_id), MINOR(dev_id));
		return tracker;
	}

	pr_info("Create tracker for device [%d:%d]\n", MAJOR(dev_id), MINOR(dev_id));
	tracker = tracker_new(dev_id);
	if (IS_ERR(tracker)) {
		pr_err("Failed to create tracker. errno=%d\n", PTR_ERR(tracker));
		return tracker;
	}

	ret = tracker_filter(tracker, filter_cmd_add);
	if (result) {
		pr_err("Failed to attach tracker. errno=%d\n", result);
		tracker_put(tracker);
		return ERR_PTR(result);
	}

	write_lock(&trackers_lock);
	list_add_tail(&trackers, tracker);
	write_unlock(&trackers_lock);

	tracker_get(tracker);
	return tracker;
}

int tracker_remove(dev_t dev_id)
{
	int ret;
	struct tracker *tracker = NULL;

	pr_info("Removing device [%d:%d] from tracking\n", MAJOR(dev_id), MINOR(dev_id));
	tracker = tracker_get_by_dev_id(dev_id);
	if (!tracker) {
		pr_err("Unable to remove device [%d:%d] from tracking: ",
			MAJOR(dev_id), MINOR(dev_id));
		return -ENODATA;
	}

	if (atomic_read(tracker->snapshot_is_taken)) {
		pr_err("Unable to remove device [%d:%d] from tracking: ",
			MAJOR(dev_id), MINOR(dev_id));
		pr_err("snapshot [0x%llx] already exist\n", tracker->snapshot_id);
		ret = -EBUSY;
		goto out;
	}

	ret = tracker_filter(tracker, filter_cmd_del);
	if (ret)
		pr_err("Failed to remove tracker from device [%d:%d]",
			MAJOR(dev_id), MINOR(dev_id));

	write_lock(&trackers_lock);
	list_add_tail(&trackers, tracker);
	write_unlock(&trackers_lock);
	tracker_put(tracker);
out:
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
		pr_err("Unable to read CBT bitmap for device [%d:%d]: ",
			MAJOR(dev_id), MINOR(dev_id));
		pr_err("device not found\n");
		return -ENODATA;

	if (atomic_read(&tracker->snapshot_is_taken))
		ret = cbt_map_read_to_user(tracker->cbt_map, user_buff, offset, length);
	else {
		pr_err("Unable to read CBT bitmap for device [%d:%d]: ",
			MAJOR(dev_id), MINOR(dev_id));
		pr_err("device is not captured by snapshot\n");
		ret = -EPERM;
	}

	tracker_put(tracker);
	return ret;
}

int tracker_collect(int max_count, struct blk_snap_cbt_info *cbt_info, int *pcount)
{
	int result = 0;
	int count = 0;
	struct tracker *tracker;

	read_lock(&trackers_lock);
	if (list_empty(&trackers)) {
		result = -ENODATA;
		goto out;
	}

	if (!cbt_info) {
		/**
		 * Just calculate trackers list length
		 */
		list_for_each_entry(tracker, &trackers, list) {
			++count;
		}
		goto out;
	}

	list_for_each_entry(tracker, &trackers, list) {
		if (count >= max_count) {
			result = -ENOBUFS;
			break;
		}

		cbt_info[count].dev_id = (__u32)tracker->dev_id;
		cbt_info[count].dev_capacity = (__u64)(bdev_nr_sectors(tracker->target_dev) << SECTOR_SHIFT);

		if (tracker->cbt_map) {
			cbt_info[count].blk_size = (__u32)cbt_map_blk_size(tracker->cbt_map);
			cbt_info[count].blk_count = (__u32)tracker->cbt_map->blk_count;
			cbt_info[count].snap_number = (__u8)tracker->cbt_map->snap_number_previous;
			uuid_copy((uuid_t *)(cbt_info[count].generationId),
				  &tracker->cbt_map->generationId);
		} else {
			cbt_info[count].blk_size = 0;
			cbt_info[count].blk_count = 0;
			cbt_info[count].snap_number = 0;
		}

		++count;
	}
out:
	read_unlock(&trackers_lock);

	*pcount = count;
	return result;
}

int tracker_mark_dirty_blocks(dev_t dev_id, struct block_range_s *block_ranges,
				unsigned int count)
{
	struct tracker *tracker
	size_t inx;
	int res = 0;

	pr_info("Marking [%d] dirty blocks for device [%d:%d]\n", count, MAJOR(dev_id),
		MINOR(dev_id));

	tracker = tracker_get_by_dev_id(dev_id);
	if (tracker == NULL) {
		pr_err("Cannot find device [%d:%d]\n", MAJOR(dev_id), MINOR(image_dev_id));
		return = -ENODEV;
	}

	for (inx = 0; inx < count; inx++) {
		res = cbt_map_set_both(tracker->cbt_map,
				       (sector_t)block_ranges[inx].sector_offset,
				       (sector_t)block_ranges[inx].sector_count);
		if (res != 0) {
			pr_err("Failed to set CBT table. errno=%d\n", res);
			break;
		}
	}
	tracker_put(tracker);
	return res;
}
