/* SPDX-License-Identifier: GPL-2.0 */
#pragma once
#include <linux/kref.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>
#include <linux/blkdev.h>
#include <linux/fs.h>

struct cbt_map;
struct diff_area;

/**
 * struct tracker - Tracker for block device.
 * 
 * @link:
 * 
 * @kref:
 * 	Protects the structure from being released during the processing of
 * 	a IOCTL.
 * @dev_id:
 * 	Original block device ID.
 * @snapshot_is_taken:
 * 
 * @cbt_map:
 * 
 * @diff_area:
 * 
 */
struct tracker {
	struct list_head link;
	struct kref kref;
	dev_t dev_id;

	atomic_t snapshot_is_taken;

	struct cbt_map *cbt_map;
	struct diff_area *diff_area;
};

void tracker_free(struct kref *kref);
static inline 
void tracker_get(struct tracker *tracker);
{
	kref_get(&tracker->kref);
};
static inline 
void tracker_put(struct tracker *tracker)
{
	if (likely(tracker))
		kref_put(&tracker->kref, tracker_free);
};
struct tracker *tracker_get_by_dev_id(dev_t dev_id);

int tracker_init(void);
void tracker_done(void);

struct tracker *tracker_create_or_get(dev_t dev_id);
int tracker_remove(dev_t dev_id);
int tracker_collect(int max_count, struct blk_snap_cbt_info *cbt_info,
                    int *p_count);
int tracker_read_cbt_bitmap(dev_t dev_id, unsigned int offset, size_t length,
			     char __user *user_buff);
int tracker_mark_dirty_blocks(dev_t dev_id,
                              struct blk_snap_block_range *block_ranges,
                              unsigned int count);

int tracker_take_snapshot(struct tracker *tracker);
void tracker_release_snapshot(struct tracker *tracker);

#if defined(HAVE_SUPER_BLOCK_FREEZE)
static inline
int _freeze_bdev(struct block_device *bdev, struct super_block **psuperblock)
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
static inline
void _thaw_bdev(struct block_device *bdev, struct super_block *superblock)
{
	if (superblock == NULL)
		return;

	if (thaw_bdev(bdev, superblock))
		pr_err("Failed to unfreeze device [%d:%d]\n",
		       MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev));
	else
		pr_info("Device [%d:%d] was unfrozen\n",
		        MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev));
}
#endif
