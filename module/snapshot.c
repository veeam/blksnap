// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME "-snapshot: " fmt

#include <linux/slab.h>
#include <linux/sched/mm.h>
#ifdef STANDALONE_BDEVFILTER
#include "blksnap.h"
#else
#include <uapi/linux/blksnap.h>
#endif
#include "memory_checker.h"
#include "snapshot.h"
#include "tracker.h"
#include "diff_storage.h"
#include "diff_area.h"
#include "snapimage.h"
#include "cbt_map.h"
#ifdef STANDALONE_BDEVFILTER
#include "log.h"
#endif

#ifdef STANDALONE_BDEVFILTER
#include "bdevfilter.h"
#endif

LIST_HEAD(snapshots);
DECLARE_RWSEM(snapshots_lock);

#if defined(BLK_SNAP_SEQUENTALFREEZE)
/*
 * snapshot_release_trackers - Releases snapshots trackers
 *
 * The sequential algorithm allows to freeze block devices one at a time.
 */
static void snapshot_release_trackers(struct snapshot *snapshot)
{
	int inx;

	pr_info("Sequentially release snapshots trackers\n");

	for (inx = 0; inx < snapshot->count; ++inx) {
		struct tracker *tracker = snapshot->tracker_array[inx];
#if defined(HAVE_SUPER_BLOCK_FREEZE)
		struct super_block *sb = NULL;
#else
		bool is_frozen = false;
#endif
		if (!tracker || !tracker->diff_area)
			continue;

		/* Flush and freeze fs */
#if defined(HAVE_SUPER_BLOCK_FREEZE)
		_freeze_bdev(tracker->diff_area->orig_bdev, &sb);
#else
		if (freeze_bdev(tracker->diff_area->orig_bdev))
			pr_err("Failed to freeze device [%u:%u]\n",
			       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
		else {
			is_frozen = true;
			pr_debug("Device [%u:%u] was frozen\n",
				MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
		}
#endif

		/* Set tracker as available for new snapshots. */
		tracker_release_snapshot(tracker);

		/* Thaw fs */
#if defined(HAVE_SUPER_BLOCK_FREEZE)
		_thaw_bdev(tracker->diff_area->orig_bdev, sb);
#else
		if (!is_frozen)
			continue;
		if (thaw_bdev(tracker->diff_area->orig_bdev))
			pr_err("Failed to thaw device [%u:%u]\n",
			       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
		else
			pr_debug("Device [%u:%u] was unfrozen\n",
				MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
#endif
	}
}

#else /* BLK_SNAP_SEQUENTALFREEZE */

/*
 * snapshot_release_trackers - Releases snapshots trackers
 *
 * The simultaneous algorithm allows to freeze all the snapshot block devices.
 */
static void snapshot_release_trackers(struct snapshot *snapshot)
{
	int inx;

	/* Flush and freeze fs on each original block device. */
	for (inx = 0; inx < snapshot->count; ++inx) {
		struct tracker *tracker = snapshot->tracker_array[inx];

		if (!tracker || !tracker->diff_area)
			continue;

#if defined(HAVE_SUPER_BLOCK_FREEZE)
		_freeze_bdev(tracker->diff_area->orig_bdev,
			     &snapshot->superblock_array[inx]);
#else
		if (freeze_bdev(tracker->diff_area->orig_bdev))
			pr_err("Failed to freeze device [%u:%u]\n",
			       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
		else
			pr_debug("Device [%u:%u] was frozen\n",
				MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
#endif
	}

	/* Set tracker as available for new snapshots. */
	for (inx = 0; inx < snapshot->count; ++inx)
		tracker_release_snapshot(snapshot->tracker_array[inx]);

	/* Thaw fs on each original block device. */
	for (inx = 0; inx < snapshot->count; ++inx) {
		struct tracker *tracker = snapshot->tracker_array[inx];

		if (!tracker || !tracker->diff_area)
			continue;

#if defined(HAVE_SUPER_BLOCK_FREEZE)
		_thaw_bdev(tracker->diff_area->orig_bdev,
			   snapshot->superblock_array[inx]);
#else
		if (thaw_bdev(tracker->diff_area->orig_bdev))
			pr_err("Failed to thaw device [%u:%u]\n",
			       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
		else
			pr_debug("Device [%u:%u] was unfrozen\n",
				MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
#endif
	}
}

#endif /* BLK_SNAP_SEQUENTALFREEZE */

static void snapshot_release(struct snapshot *snapshot)
{
	int inx;

	pr_info("Release snapshot %pUb\n", &snapshot->id);

	/* Destroy all snapshot images. */
	for (inx = 0; inx < snapshot->count; ++inx) {
		struct snapimage *snapimage = snapshot->snapimage_array[inx];

		if (snapimage)
			snapimage_free(snapimage);
	}

	snapshot_release_trackers(snapshot);

	/* Destroy diff area for each tracker. */
	for (inx = 0; inx < snapshot->count; ++inx) {
		struct tracker *tracker = snapshot->tracker_array[inx];

		if (tracker) {
			diff_area_put(tracker->diff_area);
			tracker->diff_area = NULL;

			tracker_put(tracker);
			snapshot->tracker_array[inx] = NULL;
		}
	}
}

static void snapshot_free(struct kref *kref)
{
	struct snapshot *snapshot = container_of(kref, struct snapshot, kref);

	snapshot_release(snapshot);

	kfree(snapshot->snapimage_array);
	if (snapshot->snapimage_array)
		memory_object_dec(memory_object_snapimage_array);
	kfree(snapshot->tracker_array);
	if (snapshot->tracker_array)
		memory_object_dec(memory_object_tracker_array);

#if defined(HAVE_SUPER_BLOCK_FREEZE) && !defined(BLK_SNAP_SEQUENTALFREEZE)
	if (snapshot->superblock_array) {
		kfree(snapshot->superblock_array);
		memory_object_dec(memory_object_superblock_array);
	}
#endif

	diff_storage_put(snapshot->diff_storage);

	kfree(snapshot);
	memory_object_dec(memory_object_snapshot);
}

static inline void snapshot_get(struct snapshot *snapshot)
{
	kref_get(&snapshot->kref);
};
static inline void snapshot_put(struct snapshot *snapshot)
{
	if (likely(snapshot))
		kref_put(&snapshot->kref, snapshot_free);
};

static struct snapshot *snapshot_new(unsigned int count)
{
	int ret;
	struct snapshot *snapshot = NULL;

	snapshot = kzalloc(sizeof(struct snapshot), GFP_KERNEL);
	if (!snapshot) {
		ret = -ENOMEM;
		goto fail;
	}
	memory_object_inc(memory_object_snapshot);

	snapshot->tracker_array = kcalloc(count, sizeof(void *), GFP_KERNEL);
	if (!snapshot->tracker_array) {
		ret = -ENOMEM;
		goto fail_free_snapshot;
	}
	memory_object_inc(memory_object_tracker_array);

	snapshot->snapimage_array = kcalloc(count, sizeof(void *), GFP_KERNEL);
	if (!snapshot->snapimage_array) {
		ret = -ENOMEM;
		goto fail_free_trackers;
	}
	memory_object_inc(memory_object_snapimage_array);

#if defined(HAVE_SUPER_BLOCK_FREEZE) && !defined(BLK_SNAP_SEQUENTALFREEZE)
	snapshot->superblock_array = kcalloc(count, sizeof(void *), GFP_KERNEL);
	if (!snapshot->superblock_array) {
		ret = -ENOMEM;
		goto fail_free_snapimage;
	}
	memory_object_inc(memory_object_superblock_array);
#endif
	snapshot->diff_storage = diff_storage_new();
	if (!snapshot->diff_storage) {
		ret = -ENOMEM;
		goto fail_free_snapimage;
	}

	INIT_LIST_HEAD(&snapshot->link);
	kref_init(&snapshot->kref);
	uuid_gen(&snapshot->id);
	snapshot->is_taken = false;

	return snapshot;

fail_free_snapimage:
#if defined(HAVE_SUPER_BLOCK_FREEZE) && !defined(BLK_SNAP_SEQUENTALFREEZE)
	kfree(snapshot->superblock_array);
	if (snapshot->superblock_array)
		memory_object_dec(memory_object_superblock_array);
#endif
	kfree(snapshot->snapimage_array);
	if (snapshot->snapimage_array)
		memory_object_dec(memory_object_snapimage_array);

fail_free_trackers:
	kfree(snapshot->tracker_array);
	if (snapshot->tracker_array)
		memory_object_dec(memory_object_tracker_array);

fail_free_snapshot:
	kfree(snapshot);
	if (snapshot)
		memory_object_dec(memory_object_snapshot);
fail:
	return ERR_PTR(ret);
}

void snapshot_done(void)
{
	struct snapshot *snapshot;

	pr_debug("Cleanup snapshots\n");
	do {
		down_write(&snapshots_lock);
		snapshot = list_first_entry_or_null(&snapshots, struct snapshot,
						    link);
		if (snapshot)
			list_del(&snapshot->link);
		up_write(&snapshots_lock);

		snapshot_put(snapshot);
	} while (snapshot);
}

static inline bool blk_snap_dev_is_equal(struct blk_snap_dev *first,
				    struct blk_snap_dev *second)
{
	return (first->mj == second->mj) && (first->mn == second->mn);
}

static inline int check_same_devices(struct blk_snap_dev *devices,
				     unsigned int count)
{
	struct blk_snap_dev *first;
	struct blk_snap_dev *second;

	for (first = devices; first < (devices + (count - 1)); ++first) {
		for (second = first + 1; second < (devices + count); ++second) {
			if (blk_snap_dev_is_equal(first, second)) {
				pr_err("Unable to create snapshot: The same device [%d:%d] was added twice.\n",
					first->mj, first->mn);
				return -EINVAL;
			}
		}
	}

	return 0;
}

int snapshot_create(struct blk_snap_dev *dev_id_array, unsigned int count,
		    uuid_t *id)
{
	struct snapshot *snapshot = NULL;
	int ret;
	unsigned int inx;

	pr_info("Create snapshot for devices:\n");
	for (inx = 0; inx < count; ++inx)
		pr_info("\t%u:%u\n", dev_id_array[inx].mj,
			dev_id_array[inx].mn);

	ret = check_same_devices(dev_id_array, count);
	if (ret)
		return ret;

	snapshot = snapshot_new(count);
	if (IS_ERR(snapshot)) {
		pr_err("Unable to create snapshot: failed to allocate snapshot structure\n");
		return PTR_ERR(snapshot);
	}

	ret = -ENODEV;
	for (inx = 0; inx < count; ++inx) {
		dev_t dev_id =
			MKDEV(dev_id_array[inx].mj, dev_id_array[inx].mn);
		struct tracker *tracker;

		tracker = tracker_create_or_get(dev_id);
		if (IS_ERR(tracker)) {
			pr_err("Unable to create snapshot\n");
			pr_err("Failed to add device [%u:%u] to snapshot tracking\n",
			       MAJOR(dev_id), MINOR(dev_id));
			ret = PTR_ERR(tracker);
			goto fail;
		}

		snapshot->tracker_array[inx] = tracker;
		snapshot->count++;
	}

	down_write(&snapshots_lock);
	list_add_tail(&snapshots, &snapshot->link);
	up_write(&snapshots_lock);

	uuid_copy(id, &snapshot->id);
	pr_info("Snapshot %pUb was created\n", &snapshot->id);
	return 0;
fail:
	pr_err("Snapshot cannot be created\n");

	snapshot_put(snapshot);
	return ret;
}

static struct snapshot *snapshot_get_by_id(uuid_t *id)
{
	struct snapshot *snapshot = NULL;
	struct snapshot *s;

	down_read(&snapshots_lock);
	if (list_empty(&snapshots))
		goto out;

	list_for_each_entry(s, &snapshots, link) {
		if (uuid_equal(&s->id, id)) {
			snapshot = s;
			snapshot_get(snapshot);
			break;
		}
	}
out:
	up_read(&snapshots_lock);
	return snapshot;
}

int snapshot_destroy(uuid_t *id)
{
	struct snapshot *snapshot = NULL;

	pr_info("Destroy snapshot %pUb\n", id);
	memory_object_print(false);
	down_write(&snapshots_lock);
	if (!list_empty(&snapshots)) {
		struct snapshot *s = NULL;

		list_for_each_entry(s, &snapshots, link) {
			if (uuid_equal(&s->id, id)) {
				snapshot = s;
				list_del(&snapshot->link);
				break;
			}
		}
	}
	up_write(&snapshots_lock);

	if (!snapshot) {
		pr_err("Unable to destroy snapshot: cannot find snapshot by id %pUb\n",
		       id);
		return -ENODEV;
	}
	snapshot_put(snapshot);
	memory_object_print(false);
	memory_object_max_print();

	return 0;
}

int snapshot_append_storage(uuid_t *id, struct blk_snap_dev dev_id,
			    struct blk_snap_block_range __user *ranges,
			    unsigned int range_count)
{
	int ret = 0;
	struct snapshot *snapshot;

	snapshot = snapshot_get_by_id(id);
	if (!snapshot)
		return -ESRCH;

	ret = diff_storage_append_block(snapshot->diff_storage,
					MKDEV(dev_id.mj, dev_id.mn), ranges,
					range_count);
	snapshot_put(snapshot);
	return ret;
}

#if defined(BLK_SNAP_SEQUENTALFREEZE)

/*
 * snapshot_take_trackers - Take tracker for snapshot
 *
 * The sequential algorithm allows to freeze block devices one at a time.
 */
static int snapshot_take_trackers(struct snapshot *snapshot)
{
	int ret = 0;
	int inx;

	/* Try to flush and freeze file system on each original block device. */
	for (inx = 0; inx < snapshot->count; inx++) {
		struct tracker *tracker = snapshot->tracker_array[inx];
#if defined(HAVE_SUPER_BLOCK_FREEZE)
		struct super_block *sb;
#else
		bool is_frozen = false;
#endif
		struct block_device *orig_bdev;

		if (!tracker)
			continue;

		orig_bdev = tracker->diff_area->orig_bdev;
#if defined(HAVE_SUPER_BLOCK_FREEZE)
		_freeze_bdev(orig_bdev, &sb);
#else
		if (freeze_bdev(orig_bdev))
			pr_err("Failed to freeze device [%u:%u]\n",
			       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
		else {
			is_frozen = true;
			pr_debug("Device [%u:%u] was frozen\n",
				MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
		}
#endif


		/*
		 * Take snapshot - switch CBT tables and enable COW logic
		 * for each tracker.
		 */
		ret = tracker_take_snapshot(tracker);
		if (ret) {
			pr_err("Unable to take snapshot: failed to capture snapshot %pUb\n",
			       &snapshot->id);
			break;
		}

		/* Thaw file systems on original block devices. */
#if defined(HAVE_SUPER_BLOCK_FREEZE)
		_thaw_bdev(orig_bdev, sb);
#else
		if (!is_frozen)
			continue;
		if (thaw_bdev(orig_bdev))
			pr_err("Failed to thaw device [%u:%u]\n",
			       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
		else
			pr_debug("Device [%u:%u] was unfrozen\n",
				MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
#endif
	}

	if (!ret) {
		snapshot->is_taken = true;
		return 0;
	}

	while (inx--) {
		struct tracker *tracker = snapshot->tracker_array[inx];
#if defined(HAVE_SUPER_BLOCK_FREEZE)
		struct super_block *sb;
#endif

		if (!tracker)
			continue;

#if defined(HAVE_SUPER_BLOCK_FREEZE)
		_freeze_bdev(tracker->diff_area->orig_bdev, &sb);
#else
		if (freeze_bdev(tracker->diff_area->orig_bdev))
			pr_err("Failed to freeze device [%u:%u]\n",
			       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
		else
			pr_debug("Device [%u:%u] was frozen\n",
				MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
#endif

		tracker_release_snapshot(tracker);

#if defined(HAVE_SUPER_BLOCK_FREEZE)
		_thaw_bdev(tracker->diff_area->orig_bdev, sb);
#else
		if (thaw_bdev(tracker->diff_area->orig_bdev))
			pr_err("Failed to thaw device [%u:%u]\n",
			       MAJOR(tracker->dev_id),
			       MINOR(tracker->dev_id));
		else
			pr_debug("Device [%u:%u] was unfrozen\n",
				MAJOR(tracker->dev_id),
				MINOR(tracker->dev_id));
#endif
	}

	return ret;
}
#else /* BLK_SNAP_SEQUENTALFREEZE */

/*
 * snapshot_take_trackers - Take tracker for snapshot
 *
 * The simultaneous algorithm allows to freeze all the snapshot block devices.
 */

static int snapshot_take_trackers(struct snapshot *snapshot)
{
	int ret = 0;
	int inx;
	unsigned int current_flag;

	/* Try to flush and freeze file system on each original block device. */
	for (inx = 0; inx < snapshot->count; inx++) {
		struct tracker *tracker = snapshot->tracker_array[inx];

		if (!tracker)
			continue;

#if defined(HAVE_SUPER_BLOCK_FREEZE)
		_freeze_bdev(tracker->diff_area->orig_bdev,
			     &snapshot->superblock_array[inx]);
#else
		if (freeze_bdev(tracker->diff_area->orig_bdev))
			pr_err("Failed to freeze device [%u:%u]\n",
			       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
		else
			pr_debug("Device [%u:%u] was frozen\n",
				MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
#endif
	}

	/*
	 * Take snapshot - switch CBT tables and enable COW logic
	 * for each tracker.
	 */
	for (inx = 0; inx < snapshot->count; inx++) {
		if (!snapshot->tracker_array[inx])
			continue;

		ret = tracker_take_snapshot(snapshot->tracker_array[inx]);
		if (ret) {
			pr_err("Unable to take snapshot: failed to capture snapshot %pUb\n",
			       &snapshot->id);
			break;
		}
	}

	if (ret) {
		while (inx--) {
			struct tracker *tracker = snapshot->tracker_array[inx];

			if (tracker)
				tracker_release_snapshot(tracker);
		}
	} else
		snapshot->is_taken = true;

	/* Thaw file systems on original block devices. */
	for (inx = 0; inx < snapshot->count; inx++) {
		struct tracker *tracker = snapshot->tracker_array[inx];

		if (!tracker)
			continue;

#if defined(HAVE_SUPER_BLOCK_FREEZE)
		_thaw_bdev(tracker->diff_area->orig_bdev,
			   snapshot->superblock_array[inx]);
#else
		if (thaw_bdev(tracker->diff_area->orig_bdev))
			pr_err("Failed to thaw device [%u:%u]\n",
			       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
		else
			pr_debug("Device [%u:%u] was unfrozen\n",
				MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
#endif
	}

	return ret;
}
#endif /* BLK_SNAP_SEQUENTALFREEZE */

int snapshot_take(uuid_t *id)
{
	int ret = 0;
	struct snapshot *snapshot;
	int inx;

	snapshot = snapshot_get_by_id(id);
	if (!snapshot)
		return -ESRCH;

	if (snapshot->is_taken) {
		ret = -EALREADY;
		goto out;
	}

	if (!snapshot->count) {
		ret = -ENODEV;
		goto out;
	}

	/* Allocate diff area for each device in the snapshot. */
	for (inx = 0; inx < snapshot->count; inx++) {
		struct tracker *tracker = snapshot->tracker_array[inx];
		struct diff_area *diff_area;

		if (!tracker)
			continue;

		diff_area =
			diff_area_new(tracker->dev_id, snapshot->diff_storage);
		if (IS_ERR(diff_area)) {
			ret = PTR_ERR(diff_area);
			goto fail;
		}
		tracker->diff_area = diff_area;
	}

	ret = snapshot_take_trackers(snapshot);
	if (ret)
		goto fail;

	pr_info("Snapshot was taken successfully\n");

	/*
	 * Sometimes a snapshot is in the state of corrupt immediately
	 * after it is taken.
	 */
	for (inx = 0; inx < snapshot->count; inx++) {
		struct tracker *tracker = snapshot->tracker_array[inx];

		if (!tracker)
			continue;

		if (unlikely(diff_area_is_corrupted(tracker->diff_area))) {
			pr_err("Unable to freeze devices [%u:%u]: diff area is corrupted\n",
			       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
			ret = -EFAULT;
			goto fail;
		}
	}

	/* Create all image block devices. */
	for (inx = 0; inx < snapshot->count; inx++) {
		struct snapimage *snapimage;
		struct tracker *tracker = snapshot->tracker_array[inx];

		snapimage =
			snapimage_create(tracker->diff_area, tracker->cbt_map);
		if (IS_ERR(snapimage)) {
			ret = PTR_ERR(snapimage);
			pr_err("Failed to create snapshot image for device [%u:%u] with error=%d\n",
			       MAJOR(tracker->dev_id), MINOR(tracker->dev_id),
			       ret);
			break;
		}
		snapshot->snapimage_array[inx] = snapimage;
	}

	goto out;
fail:
	pr_err("Unable to take snapshot %pUb.\n", &snapshot->id);

	down_write(&snapshots_lock);
	list_del(&snapshot->link);
	up_write(&snapshots_lock);
	snapshot_put(snapshot);
out:
	snapshot_put(snapshot);
	return ret;
}

struct event *snapshot_wait_event(uuid_t *id, unsigned long timeout_ms)
{
	struct snapshot *snapshot;
	struct event *event;

	snapshot = snapshot_get_by_id(id);
	if (!snapshot)
		return ERR_PTR(-ESRCH);

	event = event_wait(&snapshot->diff_storage->event_queue, timeout_ms);

	snapshot_put(snapshot);
	return event;
}

int snapshot_collect(unsigned int *pcount, struct blk_snap_uuid __user *id_array)
{
	int ret = 0;
	int inx = 0;
	struct snapshot *s;

	pr_debug("Collect snapshots\n");

	down_read(&snapshots_lock);
	if (list_empty(&snapshots))
		goto out;

	if (!id_array) {
		list_for_each_entry(s, &snapshots, link)
			inx++;
		goto out;
	}

	list_for_each_entry(s, &snapshots, link) {
		if (inx >= *pcount) {
			ret = -ENODATA;
			goto out;
		}

		if (copy_to_user(id_array[inx].b, &s->id.b, sizeof(uuid_t))) {
			pr_err("Unable to collect snapshots: failed to copy data to user buffer\n");
			goto out;
		}

		inx++;
	}
out:
	up_read(&snapshots_lock);
	*pcount = inx;
	return ret;
}

int snapshot_collect_images(
	uuid_t *id, struct blk_snap_image_info __user *user_image_info_array,
	unsigned int *pcount)
{
	int ret = 0;
	int inx;
	unsigned long len;
	struct blk_snap_image_info *image_info_array = NULL;
	struct snapshot *snapshot;

	pr_debug("Collect images for snapshots\n");

	snapshot = snapshot_get_by_id(id);
	if (!snapshot)
		return -ESRCH;

	if (!snapshot->is_taken) {
		ret = -ENODEV;
		goto out;
	}

	pr_debug("Found snapshot with %d devices\n", snapshot->count);
	if (!user_image_info_array) {
		pr_debug(
			"Unable to collect snapshot images: users buffer is not set\n");
		goto out;
	}

	if (*pcount < snapshot->count) {
		ret = -ENODATA;
		goto out;
	}

	image_info_array = kcalloc(snapshot->count,
				   sizeof(struct blk_snap_image_info),
				   GFP_KERNEL);
	if (!image_info_array) {
		pr_err("Unable to collect snapshot images: not enough memory.\n");
		ret = -ENOMEM;
		goto out;
	}
	memory_object_inc(memory_object_blk_snap_image_info);

	for (inx = 0; inx < snapshot->count; inx++) {
		if (snapshot->tracker_array[inx]) {
			struct tracker *tr = snapshot->tracker_array[inx];
			int mj = MAJOR(tr->dev_id);
			int mn = MINOR(tr->dev_id);

			pr_debug("Original [%u:%u]\n", mj, mn);
			image_info_array[inx].orig_dev_id.mj = mj;
			image_info_array[inx].orig_dev_id.mn = mn;
		}

		if (snapshot->snapimage_array[inx]) {
			struct snapimage *img = snapshot->snapimage_array[inx];
			int mj = img->disk->major;
			int mn = img->disk->first_minor;

			pr_debug("Image [%u:%u]\n", mj, mn);
			image_info_array[inx].image_dev_id.mj = mj;
			image_info_array[inx].image_dev_id.mn = mn;
		}
	}

	len = copy_to_user(user_image_info_array, image_info_array,
			   snapshot->count *
				   sizeof(struct blk_snap_image_info));
	if (len != 0) {
		pr_err("Unable to collect snapshot images: failed to copy data to user buffer\n");
		ret = -ENODATA;
	}
out:
	*pcount = snapshot->count;

	kfree(image_info_array);
	if (image_info_array)
		memory_object_dec(memory_object_blk_snap_image_info);
	snapshot_put(snapshot);

	return ret;
}

int snapshot_mark_dirty_blocks(dev_t image_dev_id,
			       struct blk_snap_block_range *block_ranges,
			       unsigned int count)
{
	int ret = 0;
	int inx = 0;
	struct snapshot *s;
	struct cbt_map *cbt_map = NULL;

	pr_debug("Marking [%d] dirty blocks for device [%u:%u]\n", count,
		 MAJOR(image_dev_id), MINOR(image_dev_id));

	down_read(&snapshots_lock);
	if (list_empty(&snapshots))
		goto out;

	list_for_each_entry(s, &snapshots, link) {
		for (inx = 0; inx < s->count; inx++) {
			struct snapimage *img = s->snapimage_array[inx];
			int mj = img->disk->major;
			int mn = img->disk->first_minor;

			if (MKDEV(mj,mn) == image_dev_id) {
				cbt_map = img->cbt_map;
				break;
			}
		}

		inx++;
	}
	if (!cbt_map) {
		pr_debug("Cannot find snapshot image device [%u:%u]\n",
		       MAJOR(image_dev_id), MINOR(image_dev_id));
		ret = -ENODEV;
		goto out;
	}

	ret = cbt_map_mark_dirty_blocks(cbt_map, block_ranges, count);
	if (ret)
		pr_err("Failed to set CBT table. errno=%d\n", abs(ret));
out:
	up_read(&snapshots_lock);

	return ret;
}

#ifdef BLK_SNAP_DEBUG_SECTOR_STATE
int snapshot_get_chunk_state(dev_t image_dev_id, sector_t sector,
			     struct blk_snap_sector_state *state)
{
	int ret = 0;
	int inx = 0;
	struct snapshot *s;
	struct snapimage *image = NULL;

	down_read(&snapshots_lock);
	if (list_empty(&snapshots))
		goto out;

	list_for_each_entry (s, &snapshots, link) {
		for (inx = 0; inx < s->count; inx++) {
			struct snapimage *img = s->snapimage_array[inx];
			int mj = img->disk->major;
			int mn = img->disk->first_minor;

			if (MKDEV(mj,mn) == image_dev_id) {
				image = img;
				break;
			}
		}

		inx++;
	}
	if (!image) {
		pr_err("Cannot find snapshot image device [%u:%u]\n",
		       MAJOR(image_dev_id), MINOR(image_dev_id));
		ret = -ENODEV;
		goto out;
	}

	ret = snapimage_get_chunk_state(image, sector, state);
	if (ret)
		pr_err("Failed to get chunk state. errno=%d\n", abs(ret));
out:
	up_read(&snapshots_lock);

	return ret;
}
#endif
