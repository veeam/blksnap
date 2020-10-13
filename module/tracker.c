// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-tracker"
#include "common.h"
#include "tracker.h"
#include "blk_util.h"
#include "params.h"

LIST_HEAD(trackers);
DEFINE_RWLOCK(trackers_lock);

void tracker_done(void)
{
	tracker_remove_all();
}

int tracker_find_by_bio(struct bio *bio, struct tracker **ptracker)
{
	int result = -ENODATA;

	read_lock(&trackers_lock);
	if (!list_empty(&trackers)) {
		struct list_head *_head;

		list_for_each(_head, &trackers) {
			struct tracker *_tracker = list_entry(_head, struct tracker, link);

			if ((bio->bi_disk == _tracker->target_dev->bd_disk) &&
			    (bio->bi_partno == _tracker->target_dev->bd_partno)) {
				if (ptracker != NULL)
					*ptracker = _tracker;

				result = SUCCESS;
				break;
			}
		}
	}
	read_unlock(&trackers_lock);

	return result;
}

int tracker_find_by_dev_id(dev_t dev_id, struct tracker **ptracker)
{
	int result = -ENODATA;

	read_lock(&trackers_lock);
	if (!list_empty(&trackers)) {
		struct list_head *_head;

		list_for_each(_head, &trackers) {
			struct tracker *_tracker = list_entry(_head, struct tracker, link);

			if (_tracker->original_dev_id == dev_id) {
				if (ptracker != NULL)
					*ptracker = _tracker;

				result = SUCCESS;
				break;
			}
		}
	}
	read_unlock(&trackers_lock);

	return result;
}

int tracker_enum_cbt_info(int max_count, struct cbt_info_s *p_cbt_info, int *p_count)
{
	int result = SUCCESS;
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
				p_cbt_info[count].dev_id.major = MAJOR(tracker->original_dev_id);
				p_cbt_info[count].dev_id.minor = MINOR(tracker->original_dev_id);

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

	if (result == SUCCESS)
		if (count == 0)
			result = -ENODATA;

	*p_count = count;
	return result;
}

static void blk_thaw_bdev(dev_t dev_id, struct block_device *device,
					 struct super_block *superblock)
{
	if (superblock == NULL)
		return;

	if (thaw_bdev(device, superblock) == SUCCESS)
		pr_info("Device [%d:%d] was unfrozen\n", MAJOR(dev_id), MINOR(dev_id));
	else
		pr_err("Failed to unfreeze device [%d:%d]\n", MAJOR(dev_id), MINOR(dev_id));
}

static int blk_freeze_bdev(dev_t dev_id, struct block_device *device,
			   struct super_block **psuperblock)
{
	struct super_block *superblock;

	if (device->bd_super == NULL) {
		pr_warn("Unable to freeze device [%d:%d]: no superblock was found\n",
			MAJOR(dev_id), MINOR(dev_id));
		return SUCCESS;
	}

	superblock = freeze_bdev(device);
	if (IS_ERR_OR_NULL(superblock)) {
		int result;

		pr_err("Failed to freeze device [%d:%d]\n", MAJOR(dev_id), MINOR(dev_id));

		if (superblock == NULL)
			result = -ENODEV;
		else {
			result = PTR_ERR(superblock);
			pr_err("Error code: %d\n", result);
		}
		return result;
	}

	pr_info("Device [%d:%d] was frozen\n", MAJOR(dev_id), MINOR(dev_id));
	*psuperblock = superblock;

	return SUCCESS;
}

int _tracker_create(struct tracker *tracker, void *filter, bool attach_filter)
{
	int result = SUCCESS;
	unsigned int sect_in_block_degree;
	sector_t capacity;
	struct super_block *superblock = NULL;

	result = blk_dev_open(tracker->original_dev_id, &tracker->target_dev);
	if (result != SUCCESS)
		return ENODEV;

	pr_info("Create tracker for device [%d:%d]. Capacity 0x%llx sectors\n",
		MAJOR(tracker->original_dev_id), MINOR(tracker->original_dev_id),
		(unsigned long long)part_nr_sects_read(tracker->target_dev->bd_part));

	sect_in_block_degree = get_change_tracking_block_size_pow() - SECTOR_SHIFT;
	capacity = part_nr_sects_read(tracker->target_dev->bd_part);

	tracker->cbt_map = cbt_map_create(sect_in_block_degree, capacity);
	if (tracker->cbt_map == NULL) {
		pr_err("Failed to create tracker for device [%d:%d]\n",
		       MAJOR(tracker->original_dev_id), MINOR(tracker->original_dev_id));
		tracker_remove(tracker);
		return -ENOMEM;
	}

	tracker->snapshot_id = 0ull;

	if (attach_filter) {
		blk_freeze_bdev(tracker->original_dev_id, tracker->target_dev, &superblock);
		blk_filter_freeze(tracker->target_dev);

		blk_filter_attach(tracker->original_dev_id, filter, tracker);

		blk_filter_thaw(tracker->target_dev);
		blk_thaw_bdev(tracker->original_dev_id, tracker->target_dev, superblock);
	}

	return SUCCESS;
}

int tracker_create(dev_t dev_id, void *filter, struct tracker **ptracker)
{
	int ret;
	struct tracker *tracker = NULL;

	*ptracker = NULL;

	tracker = kzalloc(sizeof(struct tracker), GFP_KERNEL);
	if (tracker == NULL)
		return -ENOMEM;

	INIT_LIST_HEAD(&tracker->link);
	atomic_set(&tracker->is_captured, false);
	tracker->original_dev_id = dev_id;

	write_lock(&trackers_lock);
	list_add_tail(&tracker->link, &trackers);
	write_unlock(&trackers_lock);

	ret = _tracker_create(tracker, filter, true);
	if (ret < 0) {
		tracker_remove(tracker);
		return ret;
	}

	*ptracker = tracker;
	if (ret == ENODEV)
		pr_info("Cannot attach to unknown device [%d:%d]",
		       MAJOR(tracker->original_dev_id), MINOR(tracker->original_dev_id));

	return ret;
}

void _tracker_remove(struct tracker *tracker, bool detach_filter)
{
	struct super_block *superblock = NULL;

	if (tracker->target_dev != NULL) {
		if (detach_filter) {
			blk_freeze_bdev(tracker->original_dev_id, tracker->target_dev, &superblock);
			blk_filter_freeze(tracker->target_dev);

			blk_filter_detach(tracker->original_dev_id);

			blk_filter_thaw(tracker->target_dev);
			blk_thaw_bdev(tracker->original_dev_id, tracker->target_dev, superblock);
		}

		blk_dev_close(tracker->target_dev);
		tracker->target_dev = NULL;
	}

	if (tracker->cbt_map != NULL) {
		cbt_map_put_resource(tracker->cbt_map);
		tracker->cbt_map = NULL;
	}
}

void tracker_remove(struct tracker *tracker)
{
	_tracker_remove(tracker, true);

	write_lock(&trackers_lock);
	list_del(&tracker->link);
	write_unlock(&trackers_lock);

	kfree(tracker);
}

void tracker_remove_all(void)
{
	struct tracker *tracker;

	pr_info("Removing all devices from tracking\n");

	do {
		tracker = NULL;

		write_lock(&trackers_lock);
		if (!list_empty(&trackers)) {
			tracker = list_entry(trackers.next, struct tracker, link);

			list_del(&tracker->link);
		}
		write_unlock(&trackers_lock);

		if (tracker) {
			_tracker_remove(tracker, true);
			kfree(tracker);
		}
	} while (tracker);
}

void tracker_cbt_bitmap_set(struct tracker *tracker, sector_t sector, sector_t sector_cnt)
{
	if (tracker->cbt_map == NULL)
		return;

	if (tracker->cbt_map->device_capacity != part_nr_sects_read(tracker->target_dev->bd_part)) {
		pr_warn("Device resize detected\n");
		tracker->cbt_map->active = false;
		return;
	}

	if (cbt_map_set(tracker->cbt_map, sector, sector_cnt) != SUCCESS) { //cbt corrupt
		pr_warn("CBT fault detected\n");
		tracker->cbt_map->active = false;
		return;
	}
}

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

int _tracker_capture_snapshot(struct tracker *tracker)
{
	int result = SUCCESS;

	result = defer_io_create(tracker->original_dev_id, tracker->target_dev, &tracker->defer_io);
	if (result != SUCCESS) {
		pr_err("Failed to create defer IO processor\n");
		return result;
	}

	atomic_set(&tracker->is_captured, true);

	if (tracker->cbt_map != NULL) {
		cbt_map_write_lock(tracker->cbt_map);
		cbt_map_switch(tracker->cbt_map);
		cbt_map_write_unlock(tracker->cbt_map);

		pr_info("Snapshot captured for device [%d:%d]. New snap number %ld\n",
			MAJOR(tracker->original_dev_id), MINOR(tracker->original_dev_id),
			tracker->cbt_map->snap_number_active);
	}

	return result;
}

int tracker_capture_snapshot(dev_t *dev_id_set, int dev_id_set_size)
{
	int result = SUCCESS;
	int inx = 0;

	for (inx = 0; inx < dev_id_set_size; ++inx) {
		struct super_block *superblock = NULL;
		struct tracker *tracker = NULL;
		dev_t dev_id = dev_id_set[inx];

		result = tracker_find_by_dev_id(dev_id, &tracker);
		if (result != SUCCESS) {
			pr_err("Unable to capture snapshot: cannot find device [%d:%d]\n",
			       MAJOR(dev_id), MINOR(dev_id));
			break;
		}


		blk_freeze_bdev(tracker->original_dev_id, tracker->target_dev, &superblock);
		blk_filter_freeze(tracker->target_dev);

		result = _tracker_capture_snapshot(tracker);
		if (result != SUCCESS)
			pr_err("Failed to capture snapshot for device [%d:%d]\n",
			       MAJOR(dev_id), MINOR(dev_id));

		blk_filter_thaw(tracker->target_dev);
		blk_thaw_bdev(tracker->original_dev_id, tracker->target_dev, superblock);
	}
	if (result != SUCCESS)
		return result;

	for (inx = 0; inx < dev_id_set_size; ++inx) {
		struct tracker *tracker = NULL;
		dev_t dev_id = dev_id_set[inx];

		result = tracker_find_by_dev_id(dev_id, &tracker);
		if (result != SUCCESS) {
			pr_err("Unable to capture snapshot: cannot find device [%d:%d]\n",
			       MAJOR(dev_id), MINOR(dev_id));
			continue;
		}

		if (snapstore_device_is_corrupted(tracker->defer_io->snapstore_device)) {
			pr_err("Unable to freeze devices [%d:%d]: snapshot data is corrupted\n",
			       MAJOR(dev_id), MINOR(dev_id));
			result = -EDEADLK;
			break;
		}
	}

	if (result != SUCCESS) {
		pr_err("Failed to capture snapshot. errno=%d\n", result);

		tracker_release_snapshot(dev_id_set, dev_id_set_size);
	}
	return result;
}

void _tracker_release_snapshot(struct tracker *tracker)
{
	struct super_block *superblock = NULL;
	struct defer_io *defer_io = tracker->defer_io;

	blk_freeze_bdev(tracker->original_dev_id, tracker->target_dev, &superblock);
	blk_filter_freeze(tracker->target_dev);
	{ //locked region
		atomic_set(&tracker->is_captured, false); //clear freeze flag

		tracker->defer_io = NULL;
	}
	blk_filter_thaw(tracker->target_dev);

	blk_thaw_bdev(tracker->original_dev_id, tracker->target_dev, superblock);

	defer_io_stop(defer_io);
	defer_io_put_resource(defer_io);
}

void tracker_release_snapshot(dev_t *dev_id_set, int dev_id_set_size)
{
	int inx = 0;

	for (; inx < dev_id_set_size; ++inx) {
		int status;
		struct tracker *p_tracker = NULL;
		dev_t dev = dev_id_set[inx];

		status = tracker_find_by_dev_id(dev, &p_tracker);
		if (status == SUCCESS)
			_tracker_release_snapshot(p_tracker);
		else
			pr_err("Unable to release snapshot: cannot find tracker for device [%d:%d]\n",
			       MAJOR(dev), MINOR(dev));
	}
}
