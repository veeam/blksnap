// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-tracking"
#include "common.h"
#include "tracking.h"
#include "tracker.h"
#include "blk_util.h"
#include "defer_io.h"
#include "params.h"

#include <linux/blk-filter.h>

/* pointer to block layer filter */
void *filter;

/*
 * _tracking_submit_bio() - Intercept bio by block io layer filter
 */
static bool _tracking_submit_bio(struct bio *bio, void *filter_data)
{
	int res;
	bool cbt_locked = false;
	struct tracker *tracker = filter_data;

	if (!tracker)
		return false;


	//intercepting
	if (atomic_read(&tracker->is_captured)) {
		//snapshot is captured, call bio redirect algorithm

		res = defer_io_redirect_bio(tracker->defer_io, bio, tracker);
		if (res == SUCCESS)
			return true;
	}

	cbt_locked = false;
	if (tracker && bio_data_dir(bio) && bio_has_data(bio)) {
		//call CBT algorithm
		cbt_locked = tracker_cbt_bitmap_lock(tracker);
		if (cbt_locked) {
			sector_t sectStart = bio->bi_iter.bi_sector;
			sector_t sectCount = bio_sectors(bio);

			tracker_cbt_bitmap_set(tracker, sectStart, sectCount);
		}
	}
	if (cbt_locked)
		tracker_cbt_bitmap_unlock(tracker);

	return false;
}

static bool _tracking_part_add(dev_t devt, void **p_filter_data)
{
	int result;
	struct tracker *tracker = NULL;

	pr_info("new block device [%d:%d] in system\n", MAJOR(devt), MINOR(devt));

	result = tracker_find_by_dev_id(devt, &tracker);
	if (result != SUCCESS)
		return false; /*do not track this device*/

	if (_tracker_create(tracker, filter, false)) {
		pr_err("Failed to attach new device to tracker. errno=%d\n", result);
		return false; /*failed to attach new device to tracker*/
	}

	*p_filter_data = tracker;
	return true;
}

static void _tracking_part_del(void *private_data)
{
	struct tracker *tracker = private_data;

	if (!tracker)
		return;

	pr_info("delete block device [%d:%d] from system\n",
		MAJOR(tracker->original_dev_id), MINOR(tracker->original_dev_id));

	_tracker_remove(tracker, false);
}

struct blk_filter_ops filter_ops = {
	.filter_bio = _tracking_submit_bio,
	.part_add = _tracking_part_add,
	.part_del = _tracking_part_del };



int tracking_init(void)
{
	filter = blk_filter_register(&filter_ops);
	if (!filter)
		return -ENOMEM;
	return SUCCESS;
}

void tracking_done(void)
{
	if (filter) {
		blk_filter_unregister(filter);
		filter = NULL;
	}
}

static int _add_already_tracked(dev_t dev_id, unsigned long long snapshot_id,
				struct tracker *tracker)
{
	int result = SUCCESS;
	bool cbt_reset_needed = false;

	if ((snapshot_id != 0ull) && (tracker->snapshot_id == 0ull))
		tracker->snapshot_id = snapshot_id; // set new snapshot id

	if (tracker->cbt_map == NULL) {
		unsigned int sect_in_block_degree =
			get_change_tracking_block_size_pow() - SECTOR_SHIFT;
		tracker->cbt_map = cbt_map_create(sect_in_block_degree - SECTOR_SHIFT,
						  part_nr_sects_read(tracker->target_dev->bd_part));
		if (tracker->cbt_map == NULL)
			return -ENOMEM;

		// skip snapshot id
		tracker->snapshot_id = snapshot_id;
		return SUCCESS;
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
		return SUCCESS;

	_tracker_remove(tracker, true);

	result = _tracker_create(tracker, filter, true);
	if (result != SUCCESS) {
		pr_err("Failed to create tracker. errno=%d\n", result);
		return result;
	}

	tracker->snapshot_id = snapshot_id;

	return SUCCESS;
}

static int _create_new_tracker(dev_t dev_id, unsigned long long snapshot_id)
{
	int result;
	struct tracker *tracker = NULL;

	result = tracker_create(dev_id, filter, &tracker);
	if (result != SUCCESS) {
		pr_err("Failed to create tracker. errno=%d\n", result);
		return result;
	}

	tracker->snapshot_id = snapshot_id;

	return SUCCESS;
}


int tracking_add(dev_t dev_id, unsigned long long snapshot_id)
{
	int result;
	struct tracker *tracker = NULL;

	pr_info("Adding device [%d:%d] under tracking\n", MAJOR(dev_id), MINOR(dev_id));

	result = tracker_find_by_dev_id(dev_id, &tracker);
	if (result == SUCCESS) {
		//pr_info("Device [%d:%d] is already tracked\n", MAJOR(dev_id), MINOR(dev_id));
		result = _add_already_tracked(dev_id, snapshot_id, tracker);
		if (result == SUCCESS)
			result = -EALREADY;
	} else if (-ENODATA == result)
		result = _create_new_tracker(dev_id, snapshot_id);
	else {
		pr_err("Unable to add device [%d:%d] under tracking\n", MAJOR(dev_id),
			MINOR(dev_id));
		pr_err("Invalid trackers container. errno=%d\n", result);
	}

	return result;
}

int tracking_remove(dev_t dev_id)
{
	int result;
	struct tracker *tracker = NULL;

	pr_info("Removing device [%d:%d] from tracking\n", MAJOR(dev_id), MINOR(dev_id));

	result = tracker_find_by_dev_id(dev_id, &tracker);
	if (result != SUCCESS) {
		pr_err("Unable to remove device [%d:%d] from tracking: ",
		       MAJOR(dev_id), MINOR(dev_id));

		if (-ENODATA == result)
			pr_err("tracker not found\n");
		else
			pr_err("tracker container failed. errno=%d\n", result);

		return result;
	}

	if (tracker->snapshot_id != 0ull) {
		pr_err("Unable to remove device [%d:%d] from tracking: ",
		       MAJOR(dev_id), MINOR(dev_id));
		pr_err("snapshot [0x%llx] already exist\n", tracker->snapshot_id);
		return -EBUSY;
	}

	tracker_remove(tracker);

	return SUCCESS;
}

int tracking_collect(int max_count, struct cbt_info_s *p_cbt_info, int *p_count)
{
	int res = tracker_enum_cbt_info(max_count, p_cbt_info, p_count);

	if (res == SUCCESS)
		pr_info("%d devices found under tracking\n", *p_count);
	else if (res == -ENODATA) {
		pr_info("There are no devices under tracking\n");
		*p_count = 0;
		res = SUCCESS;
	} else
		pr_err("Failed to collect devices under tracking. errno=%d", res);

	return res;
}

int tracking_read_cbt_bitmap(dev_t dev_id, unsigned int offset, size_t length,
			     void __user *user_buff)
{
	int result = SUCCESS;
	struct tracker *tracker = NULL;

	result = tracker_find_by_dev_id(dev_id, &tracker);
	if (result == SUCCESS) {
		if (atomic_read(&tracker->is_captured))
			result = cbt_map_read_to_user(tracker->cbt_map, user_buff, offset, length);
		else {
			pr_err("Unable to read CBT bitmap for device [%d:%d]: ", MAJOR(dev_id),
			       MINOR(dev_id));
			pr_err("device is not captured by snapshot\n");
			result = -EPERM;
		}
	} else if (-ENODATA == result) {
		pr_err("Unable to read CBT bitmap for device [%d:%d]: ", MAJOR(dev_id),
		       MINOR(dev_id));
		pr_err("device not found\n");
	} else
		pr_err("Failed to find devices under tracking. errno=%d", result);

	return result;
}
