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
	struct tracker *tracker = container_of(kref, struct tracker, refcount);

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

int tracker_enum_cbt_info(int max_count, struct cbt_info_s *p_cbt_info, int *p_count)
{
	int result = 0;
	int count = 0;

	read_lock(&trackers_lock);
	if (!list_empty(&trackers)) {
		struct list_head *_head;

		list_for_each(_head, &trackers) {
			struct tracker *tracker = list_entry(_head, struct tracker, link);

			if (count >= max_count) {
				result = -ENOBUFS;
				break; //don`t continue
			}

			if (p_cbt_info != NULL) {
				p_cbt_info[count].dev_id.major = MAJOR(tracker->dev_id);
				p_cbt_info[count].dev_id.minor = MINOR(tracker->dev_id);

				if (tracker->cbt_map) {
					p_cbt_info[count].cbt_map_size = tracker->cbt_map->map_size;
					p_cbt_info[count].snap_number =
						(unsigned char)
							tracker->cbt_map->snap_number_previous;
					uuid_copy((uuid_t *)(p_cbt_info[count].generationId),
						  &tracker->cbt_map->generationId);
				} else {
					p_cbt_info[count].cbt_map_size = 0;
					p_cbt_info[count].snap_number = 0;
				}

				p_cbt_info[count].dev_capacity = (u64)from_sectors(
					part_nr_sects_read(tracker->target_dev->bd_part));
			}

			++count;
		}
	}
	read_unlock(&trackers_lock);

	if (!result)
		if (count == 0)
			result = -ENODATA;

	*p_count = count;

	return result;
}


#if defined(HAVE_SUPER_BLOCK_FREEZE)
static void _thaw_bdev(dev_t dev_id, struct block_device *bdev, struct super_block *superblock)
{
	if (superblock == NULL)
		return;

	if (thaw_bdev(bdev, superblock))
		pr_err("Failed to unfreeze device [%d:%d]\n", MAJOR(dev_id), MINOR(dev_id));
	else
		pr_info("Device [%d:%d] was unfrozen\n", MAJOR(dev_id), MINOR(dev_id));
}

static int _freeze_bdev(struct block_device *bdev, struct super_block **psuperblock)
{
	struct super_block *superblock;

	if (bdev->bd_super == NULL) {
		pr_warn("Unable to freeze device [%d:%d]: no superblock was found\n",
			MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev));
		return 0;
	}

	superblock = freeze_bdev(bdev);
	if (IS_ERR_OR_NULL(superblock)) {
		int result;

		pr_err("Failed to freeze device [%d:%d]\n",
		       MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev));

		if (superblock == NULL)
			result = -ENODEV;
		else {
			result = PTR_ERR(superblock);
			pr_err("Error code: %d\n", result);
		}
		return result;
	}

	pr_info("Device [%d:%d] was frozen\n",
	        MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev));
	*psuperblock = superblock;

	return 0;
}
#endif

static int tracker_submit_bio_cb(struct bio *bio, void *ctx)
{
	int err = 0;
	struct tracker *tracker = ctx;

	if (!op_is_write(bio_op(bio)))
		return FLT_ST_PASS;

	err = diff_area_copy(tracker->diff_area,
	                     bio->bi_iter.bi_sector,
	                     (sector_t)(bio->bi_iter.bi_size >> SECTOR_SHIFT),
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

static void tracker_detach_cb(void *ctx)
{
	tracker_put((struct tracker *)ctx);
}

const struct filter_operations *tracker_fops = {
	.submit_bio_cb = tracker_submit_bio_cb;
	.detach_cb = tracker_detach_cb;
}

int tracker_attach(struct tracker *tracker)
{
	int ret;
	struct block_device* bdev;
#if defined(HAVE_SUPER_BLOCK_FREEZE)
	struct super_block *superblock = NULL;
#endif
	bdev = blkdev_get_by_dev(tracker->dev_id, 0, NULL);
	if (IS_ERR(bdev))
		return PTR_ERR(bdev);

#if defined(HAVE_SUPER_BLOCK_FREEZE)
	_freeze_bdev(bdev, &superblock);
#else
	if (freeze_bdev(bdev))
		pr_err("Failed to freeze device [%d:%d]\n",
		       MAJOR(bdev->bd_dev), MINOR(dev->bd_dev));
#endif
	
	ret = filter_add(bdev, tracker_fops, tracker);

#if defined(HAVE_SUPER_BLOCK_FREEZE)
	_thaw_bdev(bdev, superblock);
#else
	if (thaw_bdev(bdev))
		pr_err("Failed to thaw device [%d:%d]\n",
		       MAJOR(bdev->bd_dev), MINOR(dev->bd_dev));
#endif

	blkdev_put(blk_dev, 0);
	return ret;
}

static int tracker_new(dev_t dev_id, unsigned long long snapshot_id)
{
	int ret;
	struct tracker *tracker = NULL;
	struct block_device* bdev;
	unsigned int sect_in_block_degree;
	sector_t capacity;

	tracker = kzalloc(sizeof(struct tracker), GFP_KERNEL);
	if (tracker == NULL)
		return = -ENOMEM;

	kref_init(&tracker->refcount);
	atomic_set(&tracker->is_busy_with_snapshot, false);
	tracker->dev_d = dev_id;
	tracker->snapshot_id = 0;

	bdev = blkdev_get_by_dev(dev_id, 0, NULL);
	if (IS_ERR(bdev)) {
		kfree(tracker);
		return PTR_ERR(bdev);
	}

	pr_info("Create tracker for device [%d:%d]. Capacity 0x%llx sectors\n",
		MAJOR(tracker->dev_id), MINOR(tracker->dev_id),
		(unsigned long long)part_nr_sects_read(bdev->bd_part));

	sect_in_block_degree = get_tracker_block_size_pow() - SECTOR_SHIFT;
	capacity = part_nr_sects_read(bdev->bd_part);

	tracker->cbt_map = cbt_map_create(sect_in_block_degree, capacity);
	if (tracker->cbt_map == NULL) {
		pr_err("Failed to create tracker for device [%d:%d]\n",
		       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
		ret = -ENOMEM;
	}

	if (ret) {
		tracker_put(tracker);
		*ptracker = NULL;
	} else {
		*ptracker = tracker;
	}
	blkdev_put(bdev, 0);

	return ret;
}

static int tracker_detach(struct tracker *tracker)
{
	int ret;
	struct block_device* bdev;
#if defined(HAVE_SUPER_BLOCK_FREEZE)
	struct super_block *superblock = NULL;
#endif

	bdev = blkdev_get_by_dev(tracker->dev_id, 0, NULL);
	if (IS_ERR(bdev))
		return PTR_ERR(bdev);

#if defined(HAVE_SUPER_BLOCK_FREEZE)
	_freeze_bdev(bdev, &superblock);
#else
	if (freeze_bdev(bdev))
		pr_err("Failed to freeze device [%d:%d]\n",
		       MAJOR(bdev->bd_dev), MINOR(dev->bd_dev));
#endif
	
	ret = filter_del(bdev);

#if defined(HAVE_SUPER_BLOCK_FREEZE)
	_thaw_bdev(bdev, superblock);
#else
	if (thaw_bdev(bdev))
		pr_err("Failed to thaw device [%d:%d]\n",
		       MAJOR(bdev->bd_dev), MINOR(dev->bd_dev));
#endif

	blkdev_put(bdev, 0);

	return ret;
}


/*
bool tracker_cbt_bitmap_lock(struct tracker *tracker)
{
	if (tracker->cbt_map == NULL)
		return false;

	cbt_map_read_lock(tracker->cbt_map);
	if (!tracker->cbt_map->active) {
		cbt_map_read_unlock(tracker->cbt_map);
		return false;
	}

	return true;
}

void tracker_cbt_bitmap_unlock(struct tracker *tracker)
{
	if (tracker->cbt_map)
		cbt_map_read_unlock(tracker->cbt_map);
}
*/
int __tracker_capture_snapshot(struct tracker *tracker)
{
	struct block_device* bdev = NULL;
	int result = 0;

	tracker->snapdev = snapstore_device_get_resource(
		snapstore_device_find_by_dev_id(tracker->dev_id));
	if (!tracker->snapdev)
		return -ENODEV;




	atomic_set(&tracker->is_busy_with_snapshot, true);

	if (tracker->cbt_map != NULL) {

		cbt_map_write_lock(tracker->cbt_map);
		cbt_map_switch(tracker->cbt_map);
		cbt_map_write_unlock(tracker->cbt_map);

		pr_info("Snapshot captured for device [%d:%d]. New snap number %ld\n",
			MAJOR(tracker->dev_id), MINOR(tracker->dev_id),
			tracker->cbt_map->snap_number_active);
	}



	return result;
}

int tracker_capture_snapshot(dev_t *dev_id_set, int dev_id_set_size)
{
	int ret = 0;
	int inx = 0;

	//to do: redesign needed.
	// should be opened all block device in same time.

	for (inx = 0; inx < dev_id_set_size; ++inx) {
		struct super_block *superblock = NULL;
		struct tracker *tracker = NULL;
		dev_t dev_id = dev_id_set[inx];
		sector_t capacity;
		struct block_device *bdev = NULL;

		ret = blk_dev_open(tracker->dev_id, &bdev);
		if (ret)
			break;

		ret = tracker_get(bdev, &tracker);
		if (ret != 0) {
			pr_err("Unable to capture snapshot: cannot find device [%d:%d]\n",
			       MAJOR(dev_id), MINOR(dev_id));
			break;
		}

		// checking that the device capacity has been changed from the previous snapshot
		capacity = part_nr_sects_read(bdev->bd_part);
		if (tracker->cbt_map->device_capacity != capacity) {
			unsigned int sect_in_block_degree;

			pr_warn("Device resize detected\n");
			sect_in_block_degree = get_tracker_block_size_pow() - SECTOR_SHIFT;
			ret = cbt_map_reset(tracker->cbt_map, sect_in_block_degree, capacity);
		}

		tracker->diff_area = diff_area_new();

		_freeze_bdev(tracker->dev_id, bdev, &superblock);
		{/* filesystem locked */
			struct gendisk *disk = bdev->bd_disk;

			blk_mq_freeze_queue(disk->queue);
			blk_mq_quiesce_queue(disk->queue);

			{/* disk queue locked */
				ret = __tracker_capture_snapshot(tracker);
				if (ret != 0)
					pr_err("Failed to capture snapshot for device [%d:%d]\n",
					       MAJOR(dev_id), MINOR(dev_id));
			}
			blk_mq_unquiesce_queue(disk->queue);
			blk_mq_unfreeze_queue(disk->queue);
		}
		_thaw_bdev(tracker->dev_id, bdev, superblock);

		blk_dev_close(bdev);
	}
	if (ret)
		return ret;

	for (inx = 0; inx < dev_id_set_size; ++inx) {
		struct tracker *tracker = NULL;
		dev_t dev_id = dev_id_set[inx];

		ret = tracker_find_by_dev_id(dev_id, &tracker);
		if (ret) {
			pr_err("Unable to capture snapshot: cannot find device [%d:%d]\n",
			       MAJOR(dev_id), MINOR(dev_id));
			continue;
		}

		if (snapstore_device_is_corrupted(tracker->snapdev)) {
			pr_err("Unable to freeze devices [%d:%d]: snapshot data is corrupted\n",
			       MAJOR(dev_id), MINOR(dev_id));
			ret = -EDEADLK;
			break;
		}
	}

	if (ret) {
		pr_err("Failed to capture snapshot. errno=%d\n", ret);

		tracker_release_snapshot(dev_id_set, dev_id_set_size);
	}
	return ret;
}

void _tracker_release_snapshot(struct tracker *tracker)
{
	struct super_block *superblock = NULL;
	struct defer_io *defer_io = tracker->defer_io;

	_freeze_bdev(tracker->snapshot_device &superblock);
	{/* filesystem locked */
		struct gendisk *disk = tracker->target_dev->bd_disk;

		blk_mq_freeze_queue(disk->queue);
		blk_mq_quiesce_queue(disk->queue);
		{/* disk queue locked */
			atomic_set(&tracker->is_busy_with_snapshot, false);

			tracker->defer_io = NULL;
		}
		blk_mq_unquiesce_queue(disk->queue);
		blk_mq_unfreeze_queue(disk->queue);
	}
	_thaw_bdev(tracker->dev_id, tracker->target_dev, superblock);

	defer_io_stop(defer_io);
	defer_io_put_resource(defer_io);
}

void tracker_release_snapshot(dev_t *dev_id_set, int dev_id_set_size)
{
	int inx;

	for (inx = 0; inx < dev_id_set_size; ++inx) {
		int status;
		struct tracker *tracker = NULL;
		dev_t dev = dev_id_set[inx];

		//to do: redisign needed
		tracker = tracker_get_by_dev_id(dev);
		if (!tracker) {
			pr_err("Unable to release snapshot: cannot find tracker for device [%d:%d]\n",
			       MAJOR(dev), MINOR(dev));
			continue;
			
		}

		_tracker_release_snapshot(tracker);
	}
}
#if 0
void tracker_cow(struct tracker *tracker, sector_t start, sector_t cnt)
{
	int ret = 0;
	uint64_t blk_inx;
	uint64_t blk_start;
	uint64_t blk_last;
	struct snapstore_device *snapdev = tracker->snapdev;

	if (unlikely(snapstore_device_is_corrupted(snapdev)))
		return;

	blk_start = start >> snapstore_block_shift();
	blk_last = (start + cnt) >> snapstore_block_shift();
	if (((start + cnt) & snapstore_block_mask()) == 0)
		--blk_last;

	for (blk_inx = blk_start; blk_inx <= blk_last; ++blk_inx) {
		struct cow_block *blk;

		blk = snapstore_device_take_cow_block(snapdev, blk_inx);
		if (unlikely(IS_ERR(blk))) {
			ret = PTR_ERR(blk);
			break;
		}

		ret = cow_block_update_state(blk, cow_state_empty, cow_state_reading);
		if (ret == -EALREADY) /* Block already reading or was read */
			continue;

		if (unlikely(ret))
			break;

		ret = blk_submit_pages(snapdev->orig_bdev, READ, 0, blk->page_array,
					     blk->rg.ofs, blk->rg.cnt, &blk->bio_cnt, blk, _read_endio);
		if (unlikely(ret))
			break;
	}

	if (unlikely(ret))
		snapstore_device_set_corrupted(snapdev, ret);

	return ret;
}
#endif
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

static int _add_already_tracked(dev_t dev_id, struct tracker *tracker)
{
	int result = 0;
	bool cbt_reset_needed = false;

	if (tracker->cbt_map == NULL) {
		tracker->cbt_map = cbt_map_create(get_tracker_block_size_pow() - SECTOR_SHIFT,
						  part_nr_sects_read(tracker->target_dev->bd_part));
		if (tracker->cbt_map == NULL)
			return -ENOMEM;

		return 0;
	}

	if (!tracker->cbt_map->active) {
		cbt_reset_needed = true;
		pr_warn("Nonactive CBT table detected. CBT fault\n");
	}

	if (tracker->cbt_map->device_capacity != part_nr_sects_read(tracker->target_dev->bd_part)) {
		cbt_reset_needed = true;
		pr_warn("Device resize detected. CBT fault\n");
	}

	if (!cbt_reset_needed)
		return 0;

	result = tracker_cbt_reset(tracker);
	if (result) {
		pr_err("Failed to create tracker. errno=%d\n", result);
		return result;
	}

	return 0;
}

int tracker_add(dev_t dev_id)
{
	int result;
	struct tracker *tracker = NULL;

	pr_info("Adding device [%d:%d] under tracking\n", MAJOR(dev_id), MINOR(dev_id));

	tracker = tracker_get_by_dev_id(dev_id);
	if (tracker){
		pr_info("Device [%d:%d] is already under tracking\n",
		        MAJOR(dev_id), MINOR(dev_id));
		result = _add_already_tracked(dev_id, tracker);
		if (!result)
			result = -EALREADY;

		tracker_put(tracker);
		return result;
	}

	tracker = tracker_new(dev_id);
	if (IS_ERR(tracker)) {
		pr_err("Failed to create tracker. errno=%d\n", PTR_ERR(tracker));
		return PTR_ERR(tracker);
	}

	result = tracker_attach(tracker);
	if (result) {
		pr_err("Failed to attach tracker. errno=%d\n", result);
		tracker_put(tracker);
		return result;
	}

	write_lock(&trackers_lock);
	list_add_tail(&trackers, tracker);
	write_unlock(&trackers_lock);

	return 0;
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

	if (atomic_read(tracker->is_busy_with_snapshot)) {
		pr_err("Unable to remove device [%d:%d] from tracking: ",
		       MAJOR(dev_id), MINOR(dev_id));
		pr_err("snapshot [0x%llx] already exist\n", tracker->snapshot_id);
		ret = -EBUSY;
		goto out;
	}

	ret = tracker_detach(tracker);
	if (ret)
		pr_err("Failed to remove tracker for device ");

	write_lock(&trackers_lock);
	list_add_tail(&trackers, tracker);
	write_unlock(&trackers_lock);
	tracker_put(tracker);
out:
	tracker_put(tracker);
	return ret;
}

int tracker_read_cbt_bitmap(dev_t dev_id, unsigned int offset, size_t length,
			     void __user *user_buff)
{
	int ret;
	struct tracker *tracker;

	tracker = tracker_get_by_dev_id(dev_id);
	if (!tracker) {
		pr_err("Unable to read CBT bitmap for device [%d:%d]: ",
			MAJOR(dev_id), MINOR(dev_id));
		pr_err("device not found\n");
		return -ENODATA;

	if (atomic_read(&tracker->is_busy_with_snapshot))
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
	int ret;

	ret = tracker_enum_cbt_info(max_count, cbt_info, pcount);
	if (!ret) {
		pr_info("%d devices found under tracking\n", *pcount);
		return 0;
	}

	if (ret == -ENODATA) {
		pr_info("There are no devices under tracking\n");
		*pcount = 0;
		return 0;
	}

	pr_err("Failed to collect devices under tracking. errno=%d", res);
	return res;
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
