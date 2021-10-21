// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME "-snapshot: " fmt
#include <linux/slab.h>
#include "blk_snap.h"
#include "snapshot.h"
#include "tracker.h"
#include "diff_storage.h"
#include "diff_area.h"
#include "snapimage.h"

LIST_HEAD(snapshots);
DECLARE_RWSEM(snapshots_lock);

static
void snapshot_release(struct snapshot *snapshot)
{
	int inx;

	//DEBUG
	pr_info("%s id=%pUb\n", __FUNCTION__, &snapshot->id);

	/* destroy all snapshot images */
	for (inx = 0; inx < snapshot->count; ++inx) {
		struct snapimage * snapimage = snapshot->snapimage_array[inx];

		if (snapimage)
			snapimage_free(snapimage);
	}

	/* flush and freeze fs on each original block device */
	for (inx = 0; inx < snapshot->count; ++inx) {
		struct tracker *tracker = snapshot->tracker_array[inx];

		if (!tracker || !tracker->diff_area)
			continue;

#if defined(HAVE_SUPER_BLOCK_FREEZE)
		_freeze_bdev(tracker->diff_area->orig_bdev, &snapshot->superblock_array[inx]);
#else
		if (freeze_bdev(tracker->diff_area->orig_bdev))
			pr_err("Failed to freeze device [%u:%u]\n",
			       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
#endif
	}

	/* Set tracker as available for new snapshots */
	for (inx = 0; inx < snapshot->count; ++inx)
		tracker_release_snapshot(snapshot->tracker_array[inx]);

	/* thaw fs on each original block device */
	for (inx = 0; inx < snapshot->count; ++inx) {
		struct tracker *tracker = snapshot->tracker_array[inx];

		if (!tracker || !tracker->diff_area)
			continue;

#if defined(HAVE_SUPER_BLOCK_FREEZE)
		_thaw_bdev(tracker->diff_area->orig_bdev, snapshot->superblock_array[inx]);
#else
		if (thaw_bdev(tracker->diff_area->orig_bdev))
			pr_err("Failed to thaw device [%u:%u]\n",
			       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
#endif
	}

	/* destroy diff area for each tracker */
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

static
void snapshot_free(struct kref *kref)
{
	struct snapshot *snapshot = container_of(kref, struct snapshot, kref);

	if (snapshot->is_taken)
		snapshot_release(snapshot);

	kfree(snapshot->snapimage_array);
	kfree(snapshot->tracker_array);
#if defined(HAVE_SUPER_BLOCK_FREEZE)
	kfree(snapshot->superblock_array);
#endif
	diff_storage_put(snapshot->diff_storage);

	kfree(snapshot);
}

static inline
void snapshot_get(struct snapshot *snapshot)
{
	kref_get(&snapshot->kref);
	//DEBUG
	//pr_info("%s - refcount=%u\n", __FUNCTION__,
	//        refcount_read(&snapshot->kref.refcount));
};
static inline
void snapshot_put(struct snapshot *snapshot)
{
	if (likely(snapshot)) {
		//DEBUG
		//pr_info("%s - refcount=%u\n", __FUNCTION__,
		//        refcount_read(&snapshot->kref.refcount));
		kref_put(&snapshot->kref, snapshot_free);
	}
};

static
struct snapshot *snapshot_new(unsigned int count)
{
	int ret;
	struct snapshot *snapshot = NULL;

	snapshot = kzalloc(sizeof(struct snapshot), GFP_KERNEL);
	if (!snapshot) {
		ret= -ENOMEM;
		goto fail;
	}

	snapshot->tracker_array = kcalloc(count, sizeof(void *), GFP_KERNEL);
	if (!snapshot->tracker_array) {
		ret = -ENOMEM;
		goto fail_free_snapshot;
	}

	snapshot->snapimage_array = kcalloc(count, sizeof(void *), GFP_KERNEL);
	if (!snapshot->snapimage_array) {
		ret = -ENOMEM;
		goto fail_free_trackers;
	}

#if defined(HAVE_SUPER_BLOCK_FREEZE)
	snapshot->superblock_array = kcalloc(count, sizeof(void *), GFP_KERNEL);
	if (!snapshot->superblock_array) {
		ret = -ENOMEM;
		goto fail_free_snapimage;
	}

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
#if defined(HAVE_SUPER_BLOCK_FREEZE)
	kfree(snapshot->superblock_array);
#endif
	kfree(snapshot->snapimage_array);
fail_free_trackers:
	kfree(snapshot->tracker_array);
fail_free_snapshot:
	kfree(snapshot);
fail:
	return ERR_PTR(ret);
}

void snapshot_done(void)
{
	struct snapshot *snapshot;

	pr_info("Cleanup snapshots\n");

	do {
		down_write(&snapshots_lock);
		snapshot = list_first_entry_or_null(&snapshots, struct snapshot, link);
		if (snapshot)
			list_del(&snapshot->link);
		up_write(&snapshots_lock);

		snapshot_put(snapshot);
	} while (snapshot);
}

int snapshot_create(struct blk_snap_dev_t *dev_id_array, unsigned int count, uuid_t *id)
{
	struct snapshot *snapshot = NULL;
	int ret;
	unsigned int inx;

	pr_info("Create snapshot for devices:\n");
	for (inx = 0; inx < count; ++inx)
		pr_info("\t%u:%u\n", dev_id_array[inx].mj, dev_id_array[inx].mn);

	snapshot = snapshot_new(count);
	if (IS_ERR(snapshot)) {
		pr_err("Unable to create snapshot: failed to allocate snapshot structure\n");
		return PTR_ERR(snapshot);
	}

	ret = -ENODEV;
	for (inx = 0; inx < count; ++inx) {
		dev_t dev_id = MKDEV(dev_id_array[inx].mj, dev_id_array[inx].mn);
		struct tracker *tracker;

		tracker = tracker_create_or_get(dev_id);
		if (IS_ERR(tracker)){
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
	pr_info("Snapshot cannot be created\n");

	snapshot_put(snapshot);
	return ret;
}

struct snapshot *snapshot_get_by_id(uuid_t *id)
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
		pr_err("Unable to destroy snapshot: cannot find snapshot by id %pUb\n", id);
		return -ENODEV;
	}

	pr_info("Destroy snapshot %pUb\n", id);
	snapshot_put(snapshot);
	return 0;
}

int snapshot_append_storage(uuid_t *id, struct blk_snap_dev_t dev_id,
			    struct big_buffer *ranges, unsigned int range_count)
{
	int ret = 0;
	struct snapshot *snapshot;

	pr_info("%s", __FUNCTION__);

	snapshot = snapshot_get_by_id(id);
	if (!snapshot)
		return -ESRCH;

	ret = diff_storage_append_block(snapshot->diff_storage,
					MKDEV(dev_id.mj, dev_id.mn),
					ranges, range_count);
	snapshot_put(snapshot);
	return ret;
}

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

	/* allocate diff area for each device in snapshot */
	for (inx = 0; inx < snapshot->count; inx++) {
		struct tracker *tracker = snapshot->tracker_array[inx];
		struct diff_area *diff_area;

		if (!tracker)
			continue;

		diff_area = diff_area_new(tracker->dev_id, snapshot->diff_storage);
		if (IS_ERR(diff_area)) {
			ret = PTR_ERR(diff_area);
			goto fail;
		}
		tracker->diff_area = diff_area;
	}

	/* try to flush and freeze file system on each original block device */
	for (inx = 0; inx < snapshot->count; inx++) {
		struct tracker *tracker = snapshot->tracker_array[inx];

		if (!tracker)
			continue;

#if defined(HAVE_SUPER_BLOCK_FREEZE)
		_freeze_bdev(tracker->diff_area->orig_bdev, &snapshot->superblock_array[inx]);
#else
		if (freeze_bdev(tracker->diff_area->orig_bdev))
			pr_err("Failed to freeze device [%u:%u]\n",
			       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
#endif
	}

	/* take snapshot - switch CBT tables and enable COW logic for each tracker */
	for (inx = 0; inx < snapshot->count; inx++) {
		if (!snapshot->tracker_array[inx])
			continue;
		ret = tracker_take_snapshot(snapshot->tracker_array[inx]);
		if (ret) {
			pr_err("Unable to take snapshot: failed to capture snapshot %pUb\n",
			       &snapshot->id);
			goto fail;
		}
	}
	snapshot->is_taken = true;
	pr_info("Snapshot was taken");

	/* thaw file systems on original block devices */
	for (inx = 0; inx < snapshot->count; inx++) {
		struct tracker *tracker = snapshot->tracker_array[inx];

		if (!tracker)
			continue;

#if defined(HAVE_SUPER_BLOCK_FREEZE)
		_thaw_bdev(tracker->diff_area->orig_bdev, snapshot->superblock_array[inx]);
#else
		if (thaw_bdev(tracker->diff_area->orig_bdev))
			pr_err("Failed to thaw device [%u:%u]\n",
			       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
#endif
	}

	/**
	 * Sometimes a snapshot is in a state of corrupt immediately
	 * after it is taken.
	 */
	for (inx = 0; inx < snapshot->count; inx++) {
		struct tracker *tracker = snapshot->tracker_array[inx];

		if (!tracker)
			continue;

		if (diff_area_is_corrupted(tracker->diff_area)) {
			pr_err("Unable to freeze devices [%u:%u]: diff area is corrupted\n",
			       MAJOR(tracker->dev_id), MINOR(tracker->dev_id));
			ret = -EFAULT;
			goto fail;
		}
	}

	/* create all image block device */
	for (inx = 0; inx < snapshot->count; inx++) {
		struct snapimage *snapimage;
		struct tracker *tracker = snapshot->tracker_array[inx];

		snapimage = snapimage_create(tracker->diff_area, tracker->cbt_map);
		if (IS_ERR(snapimage)) {
			ret = PTR_ERR(snapimage);
			pr_err("Failed to create snapshot image for device [%u:%u] with error=%d\n",
			       MAJOR(tracker->dev_id), MINOR(tracker->dev_id), ret);
			break;
		}
		snapshot->snapimage_array[inx] = snapimage;
	}

	goto out;
fail:
	pr_err("Unable to take snapshot: failed to capture snapshot %pUb\n",
	       &snapshot->id);

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

	pr_info("%s\n", __FUNCTION__);
	snapshot = snapshot_get_by_id(id);
	if (!snapshot)
		return ERR_PTR(-ESRCH);

	event = event_wait(&snapshot->diff_storage->event_queue, timeout_ms);

	snapshot_put(snapshot);
	return event;
}

static inline
int uuid_copy_to_user(uuid_t __user *dst, const uuid_t *src)
{
	int len;

	len = copy_to_user(dst, src, sizeof(uuid_t));
	if (len)
		return -ENODATA;
	return 0;
}

int snapshot_collect(unsigned int *pcount, uuid_t __user *id_array)
{
	int ret = 0;
	int inx = 0;
	struct snapshot *s;

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

		ret = uuid_copy_to_user(&id_array[inx], &s->id);
		if (ret) {
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

int snapshot_collect_images(uuid_t *id, struct blk_snap_image_info __user *user_image_info_array,
			     unsigned int *pcount)
{
	int ret = 0;
	int inx;
	unsigned long len;
	struct blk_snap_image_info *image_info_array = NULL;
	struct snapshot *snapshot;

	pr_info("%s", __FUNCTION__);

	snapshot = snapshot_get_by_id(id);
	if (!snapshot)
		return -ESRCH;

	if (!snapshot->is_taken) {
		ret = -ENODEV;
		goto out;
	}

	pr_info("Found snapshot with %d devices\n", snapshot->count);
	if (*pcount < snapshot->count) {
		ret = -ENODATA;
		goto out;
	}

	if (!user_image_info_array) {
		pr_info("Users buffer is not set\n");
		goto out;
	}

	image_info_array = kcalloc(snapshot->count, sizeof(struct blk_snap_image_info), GFP_KERNEL);
	if (!image_info_array) {
		pr_err("Unable to collect snapshot images: not enough memory.\n");
		ret = -ENOMEM;
		goto out;
	}

	for (inx=0; inx<snapshot->count; inx++) {
		if (snapshot->tracker_array[inx]) {
			dev_t orig_dev_id = snapshot->tracker_array[inx]->dev_id;

			pr_info("Original [%u:%u]\n", MAJOR(orig_dev_id), MINOR(orig_dev_id));
			image_info_array[inx].orig_dev_id.mj = MAJOR(orig_dev_id);
			image_info_array[inx].orig_dev_id.mn = MINOR(orig_dev_id);
		}

		if (snapshot->snapimage_array[inx]) {
			dev_t image_dev_id = snapshot->snapimage_array[inx]->image_dev_id;

			pr_info("Image [%u:%u]\n", MAJOR(image_dev_id), MINOR(image_dev_id));
			image_info_array[inx].image_dev_id.mj = MAJOR(image_dev_id);
			image_info_array[inx].image_dev_id.mn = MINOR(image_dev_id);
		}
	}

	len = copy_to_user(user_image_info_array, image_info_array,
			   snapshot->count * sizeof(struct blk_snap_image_info));
	if (len != 0) {
		pr_err("Unable to collect snapshot images: failed to copy data to user buffer\n");
		ret = -ENODATA;
	}
out:
	*pcount = snapshot->count;
	kfree(image_info_array);
	snapshot_put(snapshot);

	return ret;
}
