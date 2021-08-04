// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-ctrl"
#include "common.h"
#include "blk-snap-ctl.h"
#include "ctrl_fops.h"
#include "version.h"
#include "tracking.h"
#include "snapshot.h"
#include "snapstore.h"
#include "snapimage.h"
#include "tracker.h"
#include "blk_deferred.h"
#include "big_buffer.h"
#include "params.h"

#include <linux/module.h>
#include <linux/poll.h>
#include <linux/uaccess.h>

static int blk_snap_major;

static const struct file_operations ctrl_fops = {
	.owner = THIS_MODULE,
	.open = ctrl_open,
	.release = ctrl_release,
	.unlocked_ioctl = ctrl_unlocked_ioctl
};

static const struct blk_snap_version version = {
	.major = FILEVER_MAJOR,
	.minor = FILEVER_MINOR,
	.revision = FILEVER_REVISION,
	.build = 0
	.compatibility_flags = 0,
	.modification_name = {0}
};

int get_blk_snap_major(void)
{
	return blk_snap_major;
}

int ctrl_init(void)
{
	int ret;

	ret = register_chrdev(0, MODULE_NAME, &ctrl_fops);
	if (ret < 0) {
		pr_err("Failed to register a character device. errno=%d\n", blk_snap_major);
		return ret;
	}

	blk_snap_major = ret;
	pr_info("Module major [%d]\n", blk_snap_major);
	return 0;
}

void ctrl_done(void)
{
	unregister_chrdev(blk_snap_major, MODULE_NAME);
}

static int ctrl_open(struct inode *inode, struct file *fl)
{
	if (try_module_get(THIS_MODULE))
		return 0;
	return -EINVAL;
}

static int ctrl_release(struct inode *inode, struct file *fl)
{
	module_put(THIS_MODULE);
	return result;
}

static int ioctl_version(unsigned long arg)
{
	if (copy_to_user((void *)arg, &version, sizeof(version))) {
		pr_err("Unable to get version: invalid user buffer\n");
		return -ENODATA;
	}

	return 0;
}

static int ioctl_tracker_add(unsigned long arg)
{
	struct blk_snap_tracker_add karg;

	if (copy_from_user(&karg, (void *)arg, sizeof(karg))) {
		pr_err("Unable to add device under tracking: invalid user buffer\n");
		return -ENODATA;
	}

	return tracker_add((dev_t)karg.dev_id);
}

static int ioctl_tracker_remove(unsigned long arg)
{
	struct blk_snap_tracker_remove karg;

	if (copy_from_user(&karg, (void *)arg, sizeof(karg)) != 0) {
		pr_err("Unable to remove device from tracking: invalid user buffer\n");
		return -ENODATA;
	}
	return tracker_remove((dev_t)karg.dev_id));
}

static int _ioctl_tracker_collect(unsigned long arg)
{
	int res;
	struct blk_snap_tracker_collect karg;
	struct blk_snap_cbt_info *cbt_info = NULL;

	pr_info("Collecting tracking devices:\n");

	if (copy_from_user(&karg, (void *)arg, sizeof(karg))) {
		pr_err("Unable to collect tracking devices: invalid user buffer\n");
		return -ENODATA;
	}

	if (karg.cbt_info_array == NULL) {
		res = tracker_collect(0x7fffffff, NULL, &karg.count);
		if (!res) {
			if (copy_to_user((void *)arg, (void *)&karg, sizeof(karg))) {
				pr_err("Unable to collect tracking devices: invalid user buffer for arguments\n");
				res = -ENODATA;
			}
		} else
			pr_err("Failed to execute tracker_collect. errno=%d\n", res);
		return res;
	}

	cbt_info = kcalloc(karg.count, sizeof(struct blk_snap_cbt_info), GFP_KERNEL);
	if (cbt_info == NULL)
		return -ENOMEM;

	res = tracker_collect(karg.count, cbt_info, &karg.count);
	if (res) {
		pr_err("Failed to execute tracker_collect. errno=%d\n", res);
		goto fail;
	}

	if (copy_to_user(karg.cbt_info, cbt_info, karg.count * sizeof(struct blk_snap_cbt_info))) {
		pr_err("Unable to collect tracking devices: invalid user buffer for CBT info\n");
		res = -ENODATA;
		goto fail;
	}

	if (copy_to_user((void *)arg, (void *)&karg, sizeof(karg))) {
		pr_err("Unable to collect tracking devices: invalid user buffer for arguments\n");
		res = -ENODATA;
		goto fail;
	}
fail:
	kfree(cbt_info);
	return res;
}

static int ioctl_tracker_read_cbt_map(unsigned long arg)
{
	dev_t dev_id;
	struct blk_snap_tracker_read_cbt_bitmap karg;

	if (copy_from_user(&karg, (void *)arg, sizeof(karg))) {
		pr_err("Unable to read CBT map: invalid user buffer\n");
		return -ENODATA;
	}

	return tracker_read_cbt_bitmap((dev_t)karg.dev_id, karg.offset, karg.length,
					(void *)karg.buff);
}

static int ioctl_tracker_mark_dirty_blocks(unsigned long arg)
{
	int ret = 0;
	struct blk_snap_tracker_mark_dirty_blocks karg;
	struct blk_snap_block_range *dirty_blocks_array;

	if (copy_from_user(&karg, (void *)arg, sizeof(karg))) {
		pr_err("Unable to mark dirty blocks: invalid user buffer\n");
		return -ENODATA;
	}

	dirty_blocks_array = kcalloc(karg.count, sizeof(struct blk_snap_block_range), GFP_KERNEL);
	if (!dirty_blocks_array) {
		pr_err("Unable to mark dirty %d blocks.\n", karg.count);
		return -ENOMEM;
	}

	if (copy_from_user(dirty_blocks_array, (void *)karg.dirty_blocks_array,
			   karg.count, sizeof(struct blk_snap_block_range))) {
		pr_err("Unable to mark dirty blocks: invalid user buffer\n");
		ret = -ENODATA;
	} else
		ret = snapimage_mark_dirty_blocks((dev_t)karg.dev_id, dirty_blocks_array, karg.count);

	kfree(dirty_blocks_array);

	return ret;
}

static int ioctl_snapshot_create(unsigned long arg)
{
	int ret;
	struct blk_snap_snapshot_create karg;
	dev_t *dev_id_array = NULL;

	if (copy_from_user(&karg, (void *)arg, sizeof(karg))) {
		pr_err("Unable to create snapshot: invalid user buffer\n");
		return -ENODATA;
	}

	dev_id_array = kcalloc(karg.count, sizeof(dev_t), GFP_KERNEL);
	if (dev_id_array == NULL) {
		pr_err("Unable to create snapshot: too many devices %d\n", karg.count);
		return -ENOMEM;
	}

	ret = snapshot_create(dev_id_array, karg.count, &karg.id);
	kfree(dev_id_array);

	if (ret)
		return;

	if (copy_to_user((void *)arg, &karg, sizeof(karg))) {
		pr_err("Unable to create snapshot: invalid user buffer\n");
		ret = -ENODATA;
	}

	return ret;
}

static int ioctl_snapshot_destroy(unsigned long arg)
{
	blk_snap_snapshot_destroy karg;

	if (copy_from_user(&karg, (void *)arg, sizeof(karg))) {
		pr_err("Unable to destroy snapshot: invalid user buffer\n");
		return -ENODATA;
	}

	return snapshot_destroy(&karg.id);
}

static int ioctl_snapshot_append_storage(unsigned long arg)
{
	int res = 0;
	struct blk_snap_snapshot_append_storage karg;
	struct big_buffer *ranges = NULL;
	size_t ranges_buffer_size;

	if (copy_from_user(&karg, (void *)arg, sizeof(karg))) {
		pr_err("Unable to append difference storage: invalid user buffer\n");
		return -EINVAL;
	}

	ranges_buffer_size = karg.range_count sizeof(struct blk_snap_block_range);
	ranges = big_buffer_alloc(ranges_buffer_size, GFP_KERNEL);
	if (ranges == NULL) {
		pr_err("Unable to append difference storage: cannot allocate [%zu] bytes\n",
		       ranges_buffer_size);
		return -ENOMEM;
	}

	if (big_buffer_copy_from_user((void *)param.ranges, 0, ranges, ranges_buffer_size) !=
		ranges_buffer_size) {
		pr_err("Unable to add file to snapstore: invalid user buffer for parameters\n");
		big_buffer_free(ranges);
		return -ENODATA;
	}

	res = snapshot_append_storage(karg.id, (dev_t)karg.dev_id, ranges, (size_t)karg.range_count);
	big_buffer_free(ranges);

	return res;
}

static int ioctl_snapshot_take(unsigned long arg)
{
	blk_snap_snapshot_take karg;

	if (copy_from_user(&karg, (void *)arg, sizeof(karg))) {
		pr_err("Unable to take snapshot: invalid user buffer\n");
		return -ENODATA;
	}

	return snapshot_take(&karg.id);
}

static int ioctl_snapshot_wait_event(unsigned long arg)
{
	int res;
	struct blk_snap_snapshot_event karg;

	if (copy_from_user(&karg, (void *)arg, PAGE_SIZE)) {
		pr_err("Unable failed to get snapstore error code: invalid user buffer\n");
		return -EINVAL;
	}

	res = snapshot_wait_event(&karg.id, karg.timeout_ms, PAGE_SIZE - sizeof(karg),
				  (ktime_t*)&karg.time_label, &karg.code, &karg.data);
	if (res)
		return res;

	if (copy_to_user((void *)arg, &karg, PAGE_SIZE)) {
		pr_err("Unable to get snapstore error code: invalid user buffer\n");
		return -EINVAL;
	}

	return 0;
}

static int ioctl_collect_snapimages(unsigned long arg)
{
	int ret;
	struct blk_snap_snapshot_collect_images karg;

	if (copy_from_user(&karg, (void *)arg, sizeof(karg))) {
		pr_err("Unable to collect snapshot images: invalid user buffer\n");
		return -ENODATA;
	}

	ret = snapshot_collect_images(karg.count, karg.image_info_array, &karg.count);

	if (copy_to_user((void *)arg, &karg, sizeof(karg))) {
		pr_err("Unable to collect snapshot images: invalid user buffer\n");
		return -ENODATA;
	}

	return ret;
}

struct ioctl_table {
	int (*fn)(unsigned long arg);
};

static const struct ioctl_table ioctl_table[] = {
	ioctl_version,
	ioctl_tracker_add,
	ioctl_tracker_remove,
	ioctl_tracker_collect,
	ioctl_tracker_read_cbt_map,
	ioctl_tracker_mark_dirty_blocks,
	ioctl_snapshot_create,
	ioctl_snapshot_destroy,
	ioctl_snapshot_append_storage,
	ioctl_snapshot_take,
	ioctl_snapshot_wait_event,
	ioctl_snapshot_collect_images,
};

static long ctrl_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int nr = _IOC_NR(cmd);

	if (nr > (sizeof(ioctl_table) / sizeof(struct ioctl_table)))
		return -ENOTTY;

	if (!blk_snap_ioctl_table[nr].fn)
		return -ENOTTY;

	return blk_snap_ioctl_table[nr].fn(arg);
}
