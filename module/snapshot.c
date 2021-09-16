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

	if (snapshot->is_taken)
		return;

	snapshot->is_taken = false;

	/* destroy all snapshot images */
	for (inx = 0; inx < snapshot->count; ++inx) {
		if (!snapshot->snapimage_array[inx])
			continue;
		snapimage_free(snapshot->snapimage_array[inx]);
	}

	/* flush and freeze fs on each original block device */
	for (inx = 0; inx < snapshot->count; ++inx) {
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

	/* Set tracker as available */
	for (inx = 0; inx < snapshot->count; ++inx)
		tracker_release_snapshot(snapshot->tracker_array[inx]);	

	/* thaw fs on each original block device */
	for (inx = 0; inx < snapshot->count; ++inx) {
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
}

static
void snapshot_free(struct kref *kref)
{
	struct snapshot *snapshot = container_of(kref, struct snapshot, kref);
	int inx;

	snapshot_release(snapshot);

	for (inx = 0; inx < snapshot->count; ++inx) {
		struct tracker *tracker = snapshot->tracker_array[inx];

		if (!tracker)
			continue;

		diff_area_put(tracker->diff_area);
		tracker_put(tracker);
	}

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
};
static inline
void snapshot_put(struct snapshot *snapshot)
{
	if (likely(snapshot))
		kref_put(&snapshot->kref, snapshot_free);
};

static
struct snapshot *snapshot_new(unsigned int count)
{
	struct snapshot *snapshot = NULL;

	snapshot = kzalloc(sizeof(struct snapshot), GFP_KERNEL);
	if (!snapshot)
		return ERR_PTR(-ENOMEM);

	snapshot->tracker_array = kcalloc(count, sizeof(void *), GFP_KERNEL);
	if (!snapshot->tracker_array) {
		kfree(snapshot);
		return ERR_PTR(-ENOMEM);
	}

	snapshot->snapimage_array = kcalloc(count, sizeof(void *), GFP_KERNEL);
	if (!snapshot->snapimage_array) {
		kfree(snapshot->tracker_array);
		kfree(snapshot);
		return ERR_PTR(-ENOMEM);
	}

#if defined(HAVE_SUPER_BLOCK_FREEZE)
	snapshot->superblock_array = kcalloc(count, sizeof(void *), GFP_KERNEL);
	if (!snapshot->superblock_array) {
		kfree(snapshot->snapimage_array);
		kfree(snapshot->tracker_array);
		snapshot_put(snapshot);
		return ERR_PTR(-ENOMEM);
	}

#endif
	INIT_LIST_HEAD(&snapshot->link);
	kref_init(&snapshot->kref);
	uuid_gen(&snapshot->id);
	snapshot->is_taken = false;

	down_write(&snapshots_lock);
	list_add_tail(&snapshots, &snapshot->link);
	up_write(&snapshots_lock);

	return snapshot;
}

void snapshot_done(void)
{
	struct snapshot *snapshot;

	pr_info("Removing all snapshots\n");
	down_write(&snapshots_lock);
	while((snapshot = list_first_entry_or_null(&snapshots, struct snapshot, link))) {
		list_del(&snapshot->link);
		snapshot_put(snapshot);
	}
	up_write(&snapshots_lock);
}

int snapshot_create(dev_t *dev_id_array, unsigned int count, uuid_t *id)
{
	struct snapshot *snapshot = NULL;
	int ret;
	unsigned int inx;

	pr_info("Create snapshot for devices:\n");
	for (inx = 0; inx < count; ++inx)
		pr_info("\t%u:%u\n", MAJOR(dev_id_array[inx]), MINOR(dev_id_array[inx]));

	snapshot = snapshot_new(count);
	if (IS_ERR(snapshot)) {
		pr_err("Unable to create snapshot: failed to allocate snapshot structure\n");
		return PTR_ERR(snapshot);
	}

	ret = -ENODEV;
	for (inx = 0; inx < snapshot->count; ++inx) {
		struct tracker *tracker;

		tracker = tracker_create_or_get(dev_id_array[inx]);
		if (IS_ERR(tracker)){
			pr_err("Unable to create snapshot\n");
			pr_err("Failed to add device [%u:%u] to snapshot tracking\n",
			       MAJOR(dev_id_array[inx]), MINOR(dev_id_array[inx]));
			ret = PTR_ERR(tracker);
			goto fail;
		}

		snapshot->tracker_array[inx] = tracker;
	}

	uuid_copy(id, &snapshot->id);
	pr_info("Snapshot %pUb was created\n", &snapshot->id);
	return 0;
fail:
	pr_info("Snapshot cannot be created\n");

	down_write(&snapshots_lock);
	list_del(&snapshot->link);
	up_write(&snapshots_lock);

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

int snapshot_append_storage(uuid_t *id, dev_t dev_id,
                            struct big_buffer *ranges, unsigned int range_count)
{
	int ret = 0;
	struct snapshot *snapshot;

	snapshot = snapshot_get_by_id(id);
	if (!snapshot)
		return -ESRCH;

	ret = diff_storage_append_block(snapshot->diff_storage, dev_id,
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

	if (snapshot->is_taken)
		return -EALREADY;

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

	snapshot = snapshot_get_by_id(id);
	if (!snapshot)
		return ERR_PTR(-ESRCH);

	event = event_wait(&snapshot->diff_storage->event_queue, timeout_ms);

	snapshot_put(snapshot);
	return event;
}

int snapshot_collect_images(uuid_t *id, struct blk_snap_image_info __user *user_image_info_array,
			     unsigned int *pcount)
{
	int ret = 0;
	int inx;
	unsigned long len;
	struct blk_snap_image_info *image_info_array = NULL;
	struct snapshot *snapshot;

	snapshot = snapshot_get_by_id(id);
	if (!snapshot)
		return -ESRCH;

	if (*pcount < snapshot->count) {
		ret = -ENODATA;
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
			image_info_array[inx].orig_dev_id = 
				snapshot->tracker_array[inx]->dev_id;
		}

		if (snapshot->snapimage_array[inx]) {
			image_info_array[inx].image_dev_id =
				snapshot->snapimage_array[inx]->image_dev_id;
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
