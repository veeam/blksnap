// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-tracker"
#include "common.h"
#include "tracker.h"
#include "blk_util.h"

LIST_HEAD(trackers);
DEFINE_RWLOCK(trackers_lock);

void tracker_done(void)
{
	tracker_remove_all();
}

int tracker_find_by_queue(struct gendisk *disk, u8 partno, struct tracker **ptracker)
{
	int result = -ENODATA;

	read_lock(&trackers_lock);
	if (!list_empty(&trackers)) {
		struct list_head *_head;

		list_for_each(_head, &trackers) {
			struct tracker *_tracker = list_entry(_head, struct tracker, link);

			if ((disk == _tracker->target_dev->bd_disk) &&
			    (partno == _tracker->target_dev->bd_partno)) {
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
					blk_dev_get_capacity(tracker->target_dev));
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

void tracker_cbt_start(struct tracker *tracker, unsigned long long snapshot_id)
{
	tracker_snapshot_id_set(tracker, snapshot_id);
}

static struct super_block *blk_thaw_bdev(dev_t dev_id, struct block_device *device,
					 struct super_block *superblock)
{
	if (superblock == NULL)
		return NULL;

	if (thaw_bdev(device, superblock) == SUCCESS)
		pr_info("Device [%d:%d] was unfrozen\n", MAJOR(dev_id), MINOR(dev_id));
	else
		pr_err("Failed to unfreeze device [%d:%d]\n", MAJOR(dev_id), MINOR(dev_id));

	return NULL;
}

static int blk_freeze_bdev(dev_t dev_id, struct block_device *device,
			   struct super_block **psuperblock)
{
	struct super_block *superblock;

	if (device->bd_super == NULL) {
		pr_warn("Unable to freeze device [%d:%d]: no superblock was found\n", MAJOR(dev_id),
			MINOR(dev_id));
		return SUCCESS;
	}

	superblock = freeze_bdev(device);
	if (IS_ERR_OR_NULL(superblock)) {
		int errcode;
		pr_err("Failed to freeze device [%d:%d]\n", MAJOR(dev_id), MINOR(dev_id));

		if (superblock == NULL)
			errcode = -ENODEV;
		else {
			errcode = PTR_ERR(superblock);
			pr_err("Error code: %d\n", errcode);
		}
		return errcode;
	}

	pr_info("Device [%d:%d] was frozen\n", MAJOR(dev_id), MINOR(dev_id));
	*psuperblock = superblock;

	return SUCCESS;
}

int tracker_create(unsigned long long snapshot_id, dev_t dev_id, unsigned int cbt_block_size_degree,
		   struct cbt_map *cbt_map, struct tracker **ptracker)
{
	int result = SUCCESS;
	struct tracker *tracker = NULL;

	*ptracker = NULL;

	tracker = kzalloc(sizeof(struct tracker), GFP_KERNEL);
	if (tracker == NULL)
		return -ENOMEM;

	INIT_LIST_HEAD(&tracker->link);

	atomic_set(&tracker->is_captured, false);
	tracker->is_unfreezable = false;
	init_rwsem(&tracker->unfreezable_lock);

	tracker->original_dev_id = dev_id;

	result = blk_dev_open(tracker->original_dev_id, &tracker->target_dev);
	if (result != SUCCESS) {
		kfree(tracker);
		return result;
	}

	write_lock(&trackers_lock);
	list_add_tail(&tracker->link, &trackers);
	write_unlock(&trackers_lock);

	do {
		struct super_block *superblock = NULL;

		pr_info("Create tracker for device [%d:%d]. Capacity 0x%llx sectors\n",
			MAJOR(tracker->original_dev_id), MINOR(tracker->original_dev_id),
			(unsigned long long)blk_dev_get_capacity(tracker->target_dev));

		if (cbt_map)
			tracker->cbt_map = cbt_map_get_resource(cbt_map);
		else {
			tracker->cbt_map =
				cbt_map_create(cbt_block_size_degree - SECTOR_SHIFT,
					       blk_dev_get_capacity(tracker->target_dev));
			if (tracker->cbt_map == NULL) {
				result = -ENOMEM;
				break;
			}
		}

		tracker_cbt_start(tracker, snapshot_id);

		result =
			blk_freeze_bdev(tracker->original_dev_id, tracker->target_dev, &superblock);
		if (result != SUCCESS) {
			tracker->is_unfreezable = true;
			break;
		}

		superblock =
			blk_thaw_bdev(tracker->original_dev_id, tracker->target_dev, superblock);

	} while (false);

	if (result == SUCCESS) {
		*ptracker = tracker;
	} else {
		int remove_status;

		pr_err("Failed to create tracker for device [%d:%d]\n",
		       MAJOR(tracker->original_dev_id), MINOR(tracker->original_dev_id));

		remove_status = tracker_remove(tracker);
		if ((remove_status == SUCCESS) || (remove_status == -ENODEV))
			tracker = NULL;
		else
			pr_err("Failed to perfrom tracker cleanup. errno=%d\n",
			       (0 - remove_status));
	}

	return result;
}

int _tracker_remove(struct tracker *tracker)
{
	int result = SUCCESS;

	if (NULL != tracker->target_dev) {
		struct super_block *superblock = NULL;

		if (tracker->is_unfreezable)
			down_write(&tracker->unfreezable_lock);
		else
			result = blk_freeze_bdev(tracker->original_dev_id, tracker->target_dev,
						 &superblock);
/*
	!!! ToDo : remove from tracker must be in this place
*/
		if (tracker->is_unfreezable)
			up_write(&tracker->unfreezable_lock);
		else
			superblock = blk_thaw_bdev(tracker->original_dev_id, tracker->target_dev,
						   superblock);

		blk_dev_close(tracker->target_dev);

		tracker->target_dev = NULL;
	} else
		result = -ENODEV;

	if (NULL != tracker->cbt_map) {
		cbt_map_put_resource(tracker->cbt_map);
		tracker->cbt_map = NULL;
	}

	return result;
}

int tracker_remove(struct tracker *tracker)
{
	int result = _tracker_remove(tracker);

	write_lock(&trackers_lock);
	list_del(&tracker->link);
	write_unlock(&trackers_lock);

	kfree(tracker);

	return result;
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
			int status = _tracker_remove(tracker);

			if (status != SUCCESS)
				pr_err("Failed to remove device [%d:%d] from tracking. errno=%d\n",
				       MAJOR(tracker->original_dev_id),
				       MINOR(tracker->original_dev_id), 0 - status);

			kfree(tracker);
		}
	} while (tracker);
}

void tracker_cbt_bitmap_set(struct tracker *tracker, sector_t sector, sector_t sector_cnt)
{
	if (tracker->cbt_map == NULL)
		return;

	if (tracker->cbt_map->device_capacity != blk_dev_get_capacity(tracker->target_dev)) {
		pr_warn("Device resize detected\n");
		tracker->cbt_map->active = false;
		return;
	}

	if (SUCCESS != cbt_map_set(tracker->cbt_map, sector, sector_cnt)) { //cbt corrupt
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

		if (tracker->is_unfreezable)
			down_write(&tracker->unfreezable_lock);
		else
			blk_freeze_bdev(tracker->original_dev_id, tracker->target_dev, &superblock);
		{
			result = _tracker_capture_snapshot(tracker);
			if (result != SUCCESS)
				pr_err("Failed to capture snapshot for device [%d:%d]\n",
				       MAJOR(dev_id), MINOR(dev_id));
		}
		if (tracker->is_unfreezable)
			up_write(&tracker->unfreezable_lock);
		else
			superblock = blk_thaw_bdev(tracker->original_dev_id, tracker->target_dev,
						   superblock);
	}
	if (result != SUCCESS)
		return result;

	for (inx = 0; inx < dev_id_set_size; ++inx) {
		struct tracker *p_tracker = NULL;
		dev_t dev_id = dev_id_set[inx];

		result = tracker_find_by_dev_id(dev_id, &p_tracker);
		if (result != SUCCESS) {
			pr_err("Unable to capture snapshot: cannot find device [%d:%d]\n",
			       MAJOR(dev_id), MINOR(dev_id));
			continue;
		}

		if (snapstore_device_is_corrupted(p_tracker->defer_io->snapstore_device)) {
			pr_err("Unable to freeze devices [%d:%d]: snapshot data is corrupted\n",
			       MAJOR(dev_id), MINOR(dev_id));
			result = -EDEADLK;
			break;
		}
	}

	if (result != SUCCESS) {
		int status;
		pr_err("Failed to capture snapshot. errno=%d\n", result);

		status = tracker_release_snapshot(dev_id_set, dev_id_set_size);
		if (status != SUCCESS)
			pr_err("Failed to perfrom snapshot cleanup. errno=%d\n", status);
	}
	return result;
}

int _tracker_release_snapshot(struct tracker *tracker)
{
	int result = SUCCESS;
	struct super_block *superblock = NULL;
	struct defer_io *defer_io = tracker->defer_io;

	if (tracker->is_unfreezable)
		down_write(&tracker->unfreezable_lock);
	else
		result =
			blk_freeze_bdev(tracker->original_dev_id, tracker->target_dev, &superblock);
	{ //locked region
		atomic_set(&tracker->is_captured, false); //clear freeze flag

		tracker->defer_io = NULL;
	}
	if (tracker->is_unfreezable)
		up_write(&tracker->unfreezable_lock);
	else
		superblock =
			blk_thaw_bdev(tracker->original_dev_id, tracker->target_dev, superblock);

	defer_io_stop(defer_io);
	defer_io_put_resource(defer_io);

	return result;
}

int tracker_release_snapshot(dev_t *dev_id_set, int dev_id_set_size)
{
	int result = SUCCESS;
	int inx = 0;

	for (; inx < dev_id_set_size; ++inx) {
		int status;
		struct tracker *p_tracker = NULL;
		dev_t dev = dev_id_set[inx];

		status = tracker_find_by_dev_id(dev, &p_tracker);
		if (status == SUCCESS) {
			status = _tracker_release_snapshot(p_tracker);
			if (status != SUCCESS) {
				pr_err("Failed to release snapshot for device [%d:%d]\n",
				       MAJOR(dev), MINOR(dev));

				result = status;
				break;
			}
		} else
			pr_err("Unable to release snapshot: cannot find tracker for device [%d:%d]\n",
			       MAJOR(dev), MINOR(dev));
	}

	return result;
}

void tracker_snapshot_id_set(struct tracker *tracker, unsigned long long snapshot_id)
{
	tracker->snapshot_id = snapshot_id;
}

unsigned long long tracker_snapshot_id_get(struct tracker *tracker)
{
	return tracker->snapshot_id;
}
