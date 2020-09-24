// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-tracking"
#include "common.h"
#include "tracking.h"
#include "tracker.h"
#include "blk_util.h"
#include "defer_io.h"

/**
 * tracking_submit_bio() - Intercept bio by block io layer filter
 */
bool tracking_submit_bio(struct bio *bio, blk_qc_t *result)
{
	bool was_catched = false;
	struct tracker *tracker = NULL;

	bio_get(bio);

	if (SUCCESS == tracker_find_by_queue(bio->bi_disk, bio->bi_partno, &tracker)) {
		if ((bio->bi_end_io != blk_redirect_bio_endio) &&
		    (bio->bi_end_io != blk_deferred_bio_endio)) {
			if (tracker->is_unfreezable)
				down_read(&tracker->unfreezable_lock);

			if (atomic_read(&tracker->is_captured)) {
				if (SUCCESS ==
				    defer_io_redirect_bio(tracker->defer_io, bio, tracker)) {
					was_catched = true;
					*result = 0;
				}
			}

			if (tracker->is_unfreezable)
				up_read(&tracker->unfreezable_lock);
		}

		if (!was_catched) {
			bool cbt_locked = false;

			if (tracker && bio_data_dir(bio) && bio_has_data(bio)) {
				cbt_locked = tracker_cbt_bitmap_lock(tracker);
				if (cbt_locked) {
					sector_t sectStart = bio->bi_iter.bi_sector;
					sector_t sectCount = bio_sectors(bio);
					tracker_cbt_bitmap_set(tracker, sectStart, sectCount);
				}
			}
			if (cbt_locked)
				tracker_cbt_bitmap_unlock(tracker);
		}
	}

	bio_put(bio);

	return was_catched;
}

int tracking_add(dev_t dev_id, unsigned int cbt_block_size_degree, unsigned long long snapshot_id)
{
	int result = SUCCESS;
	struct tracker *tracker = NULL;

	pr_info("Adding device [%d:%d] under tracking\n", MAJOR(dev_id), MINOR(dev_id));

	result = tracker_find_by_dev_id(dev_id, &tracker);
	if (SUCCESS == result) {
		pr_info("Device [%d:%d] is already tracked\n", MAJOR(dev_id), MINOR(dev_id));

		if ((snapshot_id != 0ull) && (tracker_snapshot_id_get(tracker) == 0ull))
			tracker_snapshot_id_set(tracker, snapshot_id);

		if (NULL == tracker->cbt_map) {
			tracker->cbt_map =
				cbt_map_create((cbt_block_size_degree - SECTOR_SHIFT),
					       blk_dev_get_capacity(tracker->target_dev));
			if (tracker->cbt_map == NULL)
				result = -ENOMEM;
			else {
				tracker_cbt_start(tracker, snapshot_id);
				result = -EALREADY;
			}
		} else {
			bool reset_needed = false;
			if (!tracker->cbt_map->active) {
				reset_needed = true;
				pr_warn("Nonactive CBT table detected. CBT fault\n");
			}

			if (tracker->cbt_map->device_capacity !=
			    blk_dev_get_capacity(tracker->target_dev)) {
				reset_needed = true;
				pr_warn("Device resize detected. CBT fault\n");
			}

			if (reset_needed) {
				result = tracker_remove(tracker);
				if (SUCCESS != result) {
					pr_err("Failed to remove tracker. errno=%d\n", result);
				} else {
					result = tracker_create(snapshot_id, dev_id,
								cbt_block_size_degree, NULL,
								&tracker);
					if (SUCCESS != result) {
						pr_err("Failed to create tracker. errno=%d\n",
						       result);
					}
				}
			}
			if (result == SUCCESS)
				result = -EALREADY;
		}
	} else if (-ENODATA == result) {
		struct block_device *target_dev = NULL;
		do { //check space already under tracking

			result = blk_dev_open(dev_id, &target_dev);
			if (result != SUCCESS)
				break;

			if (SUCCESS == tracker_find_by_queue(target_dev->bd_disk,
							     target_dev->bd_partno, &tracker)) {
				result = -EALREADY;
				pr_err("Tracker queue already exist.\n");
				break;
			}

			result = tracker_create(snapshot_id, dev_id, cbt_block_size_degree, NULL,
						&tracker);
			if (SUCCESS != result)
				pr_err("Failed to create tracker. errno=%d\n", result);
			else {
				char dev_name[BDEVNAME_SIZE + 1];
				memset(dev_name, 0, BDEVNAME_SIZE + 1);
				if (bdevname(target_dev, dev_name))
					pr_info("Add to tracking device %s\n", dev_name);

				if (target_dev->bd_super) {
					pr_info("fs id: %s\n", target_dev->bd_super->s_id);
				} else
					pr_info("Filesystem not found\n");
			}
		} while (false);

		if (target_dev)
			blk_dev_close(target_dev);

	} else {
		pr_err("Unable to add device [%d:%d] under tracking\n", MAJOR(dev_id),
		       MINOR(dev_id));
		pr_err("Invalid trackers container. errno=%d\n", result);
	}

	return result;
}

int tracking_remove(dev_t dev_id)
{
	int result = SUCCESS;
	struct tracker *tracker = NULL;

	pr_info("Removing device [%d:%d] from tracking\n", MAJOR(dev_id), MINOR(dev_id));

	result = tracker_find_by_dev_id(dev_id, &tracker);
	if (SUCCESS == result) {
		if ((tracker) && (tracker_snapshot_id_get(tracker) == 0ull)) {
			result = tracker_remove(tracker);
			if (SUCCESS == result)
				tracker = NULL;
			else
				pr_err("Unable to remove device from tracking: failed to remove tracker. errno=%d\n",
				       result);
		} else {
			pr_err("Unable to remove device from tracking: snapshot [0x%llx] is created\n",
			       tracker_snapshot_id_get(tracker));
			result = -EBUSY;
		}
	} else if (-ENODATA == result)
		pr_err("Unable to remove device [%d:%d] from tracking: tracker not found\n",
		       MAJOR(dev_id), MINOR(dev_id));
	else
		pr_err("Unable to remove device [%d:%d] from tracking: invalid trackers container. errno=%d\n",
		       MAJOR(dev_id), MINOR(dev_id), result);

	return result;
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
	if (SUCCESS == result) {
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
