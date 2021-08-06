// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-snapshot"
#include "common.h"
#include "snapshot.h"
#include "tracker.h"
#include "snapimage.h"
#include "tracking.h"

LIST_HEAD(snapshots);
DECLARE_RWSEM(snapshots_lock);

void snapshot_free(struct kref *kref)
{
	struct snapshot *snapshot = container_of(kref, struct snapshot, kref);
	struct snapshot_event *event;
	int inx;

	for (inx = 0; inx < snapshot->count; ++inx)
		if (snapshot->snapimage_array[inx])
			snapimage_put(snapshot->snapimage_array[inx]);
	kfree(snapshot->snapimage_array);

	for (inx = 0; inx < snapshot->count; ++inx) {
		if (snapshot->tracker_array[inx])
			tracker_put(snapshot->tracker_array[inx]);
	}
	kfree(snapshot->tracker_array);

	while ((event = list_first_entry_or_null(&snapshot->events, struct snapshot_event, link))) {
		list_del(&event->link);
		kfree(event);
	}

	if (snapshot->diff_storage)
		diff_storage_put(snapshot->diff_storage);

	kfree(snapshot);	
}

static inline void snapshot_get(struct snapshot *snapshot)
{
	kref_get(&snapshot->kref);
}
static inline void snapshot_put(struct snapshot *snapshot)
{
	kref_put(&snapshot->kref, snapshot_free);
}
/*
static void _snapshot_destroy(struct snapshot *snapshot)
{
	size_t inx;

	for (inx = 0; inx < snapshot->dev_id_set_size; ++inx)
		snapimage_stop(snapshot->dev_id_set[inx]);

	pr_info("Release snapshot [0x%llx]\n", snapshot->id);

	tracker_release_snapshot(snapshot->dev_id_set, snapshot->dev_id_set_size);

	for (inx = 0; inx < snapshot->dev_id_set_size; ++inx)
		snapimage_destroy(snapshot->dev_id_set[inx]);

	snapshot_put(snapshot);
}
*/
static struct snapshot * snapshot_new(unsigned int count)
{
	struct snapshot *snapshot = NULL;
	dev_t *snap_set = NULL;

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

	INIT_LIST_HEAD(&snapshot->link);
	kref_init(&snapshot->kref);
	uuid_gen(snapshot->id);

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
	while((snap = list_first_entry_or_null(snapshots, struct snapshot, list))) {
		list_del(&snapshot->link);
		snapshot_put(snapshot);
	}
	up_write(&snapshots_lock);
}

int snapshot_create(dev_t *dev_id_array, unsigned int count, uuid_t *id)
{
	struct snapshot *snapshot = NULL;
	int result;
	unsigned int inx;

	pr_info("Create snapshot for devices:\n");
	for (inx = 0; inx < count; ++inx)
		pr_info("\t%d:%d\n", MAJOR(dev_id_array[inx]), MINOR(dev_id_array[inx]));

	snapshot = snapshot_new(count);
	if (IS_ERR(snapshot)) {
		pr_err("Unable to create snapshot: failed to allocate snapshot structure\n");
		return PTR_ERR(snapshot);
	}

	result = -ENODEV;
	for (inx = 0; inx < snapshot->count; ++inx) {
		dev_t dev_id = dev_id_array[inx];

		tracker = tracker_get_by_dev_id(dev_id);
		if (!tracker) {
			result = tracker_add(dev_id);
			if (result){
				pr_err("Unable to create snapshot\n");
				pr_err("Failed to add device [%d:%d] to snapshot tracking\n",
				       MAJOR(dev_id), MINOR(dev_id));
				goto fail;
			}			
		}

	}

	uuid_copy(id, snapshot->id);
	pr_info("Snapshot %pUb was created\n", snapshot->id);
	return 0;
fail:
	pr_info("Snapshot cannot be created\n");

	down_write(&snapshots_lock);
	list_del(&snapshot->link);
	up_write(&snapshots_lock);

	snapshot_put(snapshot);
	return result;
}

struct snapshot *snapshot_get_by_id(uuid_t *id)
{
	struct snapshot *snapshot = NULL;
	struct snapshot *_snap;

	down_read(&snapshots_lock);
	if (list_empty(&snapshots))
		goto out;

	list_for_each_entry(_snap, &snapshots, link) {
		if (_snap->id == id) {
			snapshot = _snap;
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

	pr_info("Destroy snapshot [0x%llx]\n", id);

	down_read(&snapshots_lock);
	if (!list_empty(&snapshots)) {

		list_for_each_entry(snapshot, &snapshots, link) {
			if (_snap->id == id) {
				snapshot = _snap;
				list_del(&snapshot->link);
				break;
			}
		}
	}
	up_read(&snapshots_lock);

	if (snapshot == NULL) {
		pr_err("Unable to destroy snapshot [0x%llx]: cannot find snapshot by id\n",
		       id);
		return -ENODEV;
	}

	_snapshot_destroy(snapshot);
	return 0;
}


int snapshot_take(uuid_t *id)
{
	int ret = 0;
	struct snapshot *snapshot;
	int inx;

	snapshot = snapshot_get_by_id(uuid_t *id);
	if (!snapshot)
		return -ESRCH;


	for (inx = 0; inx < snapshot->count; inx++)
		tracker_freeze(snapshot->tracker_array[inx]);

	for (inx = 0; inx < snapshot->count; inx++) {
		ret = tracker_take_snapshot(snapshot->tracker_array[inx]);
		if (ret) {
			pr_err("Unable to take snapshot: failed to capture snapshot %pUb\n",
			       snapshot->id);
			break;
		}
	}

	if (ret)
		while ((--inx) >= 0)
			tracker_release_snapshot(snapshot->tracker_array[inx]);

	for (inx = 0; inx < snapshot->count; inx++)
		tracker_thaw(snapshot->tracker_array[inx]);

	for (inx = 0; inx < snapshot->count; inx++) {
		struct tracker *tracker = snapshot->tracker_array[inx];
		struct snapimage *snapimage;

		snapimage = snapimage_create(tracker->diff_area, tracker->cbt_map);
		if (!snapimage)

		snapshot->snapimage_array[inx] = snapimage
	}

out:
	snapshot_put(snapshot);
	return ret;
}

struct snapshot_event *snapshot_wait_event(uuid_t *id, unsigned long timeout_ms)
{

}

int snapshot_collect_images(uuid_t *id, struct blk_snap_image_info __user *user_image_info_array,
			     unsigned int *pcount)
{
	int ret = 0;
	int real_count = 0;
	int count = *pcount;
	unsigned long len;
	struct blk_snap_image_info *image_info_array = NULL;

	down_read(&snap_images_lock);
	if (!list_empty(&snap_images)) {
		struct list_head *_list_head;

		list_for_each(_list_head, &snap_images)
			real_count++;
	}
	up_read(&snap_images_lock);
	*pcount = real_count;

	if (count < real_count)
		ret = -ENODATA;

	real_count = min(count, real_count);
	if (real_count == 0)
		return ret;

	image_info_array = kcalloc(real_count, sizeof(struct blk_snap_image_info), GFP_KERNEL);
	if (image_info_array == NULL) {
		pr_err("Unable to collect snapshot images: not enough memory.\n");
		return -ENOMEM;
	}

	down_read(&snap_image_destroy_lock);
	down_read(&snap_images_lock);

	if (!list_empty(&snap_images)) {
		size_t inx = 0;
		struct list_head *_list_head;

		list_for_each(_list_head, &snap_images) {
			struct snapimage *img =
				list_entry(_list_head, struct snapimage, link);

			real_count++;

			image_info_array[inx].original_dev_id.major =
				MAJOR(img->orig_dev_id);
			image_info_array[inx].original_dev_id.minor =
				MINOR(img->orig_dev_id);
			image_info_array[inx].snapshot_dev_id.major =
				MAJOR(img->image_dev);
			image_info_array[inx].snapshot_dev_id.minor =
				MINOR(img->image_dev);

			++inx;
			if (inx > real_count)
				break;
		}
	}

	up_read(&snap_images_lock);
	up_read(&snap_image_destroy_lock);

	len = copy_to_user(user_image_info_array, image_info_array,
	                   real_count * sizeof(struct blk_snap_image_info));
	if (len != 0) {
		pr_err("Unable to collect snapshot images: failed to copy data to user buffer\n");
		res = -ENODATA;
	}
fail:
	kfree(image_info_array);


	return res;
}
