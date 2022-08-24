// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME "-ctrl: " fmt

#include <linux/module.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#ifdef STANDALONE_BDEVFILTER
#include "blk_snap.h"
#else
#include <linux/blk_snap.h>
#endif
#include "memory_checker.h"
#include "ctrl.h"
#include "params.h"
#include "version.h"
#include "snapshot.h"
#include "snapimage.h"
#include "tracker.h"
#include "big_buffer.h"
#ifdef STANDALONE_BDEVFILTER
#include "log.h"
#endif

static int blk_snap_major;

static long ctrl_unlocked_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg);

static const struct file_operations ctrl_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = ctrl_unlocked_ioctl,
};

static const struct blk_snap_version version = {
	.major = VERSION_MAJOR,
	.minor = VERSION_MINOR,
	.revision = VERSION_REVISION,
	.build = VERSION_BUILD,
};

static const struct blk_snap_mod modification = {
	.name = MOD_NAME,
	.compatibility_flags =
#ifdef BLK_SNAP_DEBUG_SECTOR_STATE
	(1ull << blk_snap_compat_flag_debug_sector_state) |
#endif
#ifdef BLK_SNAP_FILELOG
	(1ull << blk_snap_compat_flag_setlog) |
#endif
	0
};

int get_blk_snap_major(void)
{
	return blk_snap_major;
}

int ctrl_init(void)
{
	int ret;

	ret = register_chrdev(0, BLK_SNAP_MODULE_NAME, &ctrl_fops);
	if (ret < 0) {
		pr_err("Failed to register a character device. errno=%d\n",
		       abs(blk_snap_major));
		return ret;
	}

	blk_snap_major = ret;
	pr_info("Register control device [%d:0].\n", blk_snap_major);
	return 0;
}

void ctrl_done(void)
{
	pr_info("Unregister control device\n");

	unregister_chrdev(blk_snap_major, BLK_SNAP_MODULE_NAME);
}

static int ioctl_version(unsigned long arg)
{
	if (copy_to_user((void *)arg, &version, sizeof(version))) {
		pr_err("Unable to get version: invalid user buffer\n");
		return -ENODATA;
	}

	return 0;
}

static int ioctl_tracker_remove(unsigned long arg)
{
	struct blk_snap_tracker_remove karg;

	if (copy_from_user(&karg, (void *)arg, sizeof(karg)) != 0) {
		pr_err("Unable to remove device from tracking: invalid user buffer\n");
		return -ENODATA;
	}
	return tracker_remove(MKDEV(karg.dev_id.mj, karg.dev_id.mn));
}

static int ioctl_tracker_collect(unsigned long arg)
{
	int res;
	struct blk_snap_tracker_collect karg;
	struct blk_snap_cbt_info *cbt_info = NULL;

	pr_debug("Collecting tracking devices\n");

	if (copy_from_user(&karg, (void *)arg, sizeof(karg))) {
		pr_err("Unable to collect tracking devices: invalid user buffer\n");
		return -ENODATA;
	}

	if (!karg.cbt_info_array) {
		/*
		 * If the buffer is empty, this is a request to determine
		 * the number of trackers.
		 */
		res = tracker_collect(0, NULL, &karg.count);
		if (res) {
			pr_err("Failed to execute tracker_collect. errno=%d\n",
			       abs(res));
			return res;
		}
		if (copy_to_user((void *)arg, (void *)&karg, sizeof(karg))) {
			pr_err("Unable to collect tracking devices: invalid user buffer for arguments\n");
			return -ENODATA;
		}
		return 0;
	}

	cbt_info = kcalloc(karg.count, sizeof(struct blk_snap_cbt_info),
			   GFP_KERNEL);
	if (cbt_info == NULL)
		return -ENOMEM;
	memory_object_inc(memory_object_blk_snap_cbt_info);

	res = tracker_collect(karg.count, cbt_info, &karg.count);
	if (res) {
		pr_err("Failed to execute tracker_collect. errno=%d\n",
		       abs(res));
		goto fail;
	}

	if (copy_to_user(karg.cbt_info_array, cbt_info,
			 karg.count * sizeof(struct blk_snap_cbt_info))) {
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
	memory_object_dec(memory_object_blk_snap_cbt_info);
	return res;
}

static int ioctl_tracker_read_cbt_map(unsigned long arg)
{
	struct blk_snap_tracker_read_cbt_bitmap karg;

	if (copy_from_user(&karg, (void *)arg, sizeof(karg))) {
		pr_err("Unable to read CBT map: invalid user buffer\n");
		return -ENODATA;
	}

	return tracker_read_cbt_bitmap(MKDEV(karg.dev_id.mj, karg.dev_id.mn),
				       karg.offset, karg.length,
				       (char __user *)karg.buff);
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

	dirty_blocks_array = kcalloc(
		karg.count, sizeof(struct blk_snap_block_range), GFP_KERNEL);
	if (!dirty_blocks_array)
		return -ENOMEM;
	memory_object_inc(memory_object_blk_snap_block_range);

	if (copy_from_user(dirty_blocks_array, (void *)karg.dirty_blocks_array,
			   karg.count * sizeof(struct blk_snap_block_range))) {
		pr_err("Unable to mark dirty blocks: invalid user buffer\n");
		ret = -ENODATA;
	} else {
		if (karg.dev_id.mj == snapimage_major())
			ret = snapshot_mark_dirty_blocks(
				MKDEV(karg.dev_id.mj, karg.dev_id.mn),
				dirty_blocks_array, karg.count);
		else
			ret = tracker_mark_dirty_blocks(
				MKDEV(karg.dev_id.mj, karg.dev_id.mn),
				dirty_blocks_array, karg.count);
	}

	kfree(dirty_blocks_array);
	memory_object_dec(memory_object_blk_snap_block_range);

	return ret;
}

static int ioctl_snapshot_create(unsigned long arg)
{
	int ret;
	struct blk_snap_snapshot_create karg;
	struct blk_snap_dev_t *dev_id_array = NULL;

	if (copy_from_user(&karg, (void *)arg, sizeof(karg))) {
		pr_err("Unable to create snapshot: invalid user buffer\n");
		return -ENODATA;
	}

	dev_id_array =
		kcalloc(karg.count, sizeof(struct blk_snap_dev_t), GFP_KERNEL);
	if (dev_id_array == NULL) {
		pr_err("Unable to create snapshot: too many devices %d\n",
		       karg.count);
		return -ENOMEM;
	}
	memory_object_inc(memory_object_blk_snap_dev_t);

	if (copy_from_user(dev_id_array, (void *)karg.dev_id_array,
			   karg.count * sizeof(struct blk_snap_dev_t))) {
		pr_err("Unable to create snapshot: invalid user buffer\n");
		ret = -ENODATA;
		goto out;
	}

	ret = snapshot_create(dev_id_array, karg.count, &karg.id);
	if (ret)
		goto out;

	if (copy_to_user((void *)arg, &karg, sizeof(karg))) {
		pr_err("Unable to create snapshot: invalid user buffer\n");
		ret = -ENODATA;
	}
out:
	kfree(dev_id_array);
	memory_object_dec(memory_object_blk_snap_dev_t);
	return ret;
}

static int ioctl_snapshot_destroy(unsigned long arg)
{
	struct blk_snap_snapshot_destroy karg;

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

	pr_debug("Append difference storage\n");

	if (copy_from_user(&karg, (void *)arg, sizeof(karg))) {
		pr_err("Unable to append difference storage: invalid user buffer\n");
		return -EINVAL;
	}

	/*
	 * Rarely, but there are file systems in which the blocks on the disk
	 * are significantly fragmented. And the drive for the diff storage can be
	 * quite large.
	 * At the same time, an attempt to allocate several pages of continuous
	 * address space on such systems often causes an ENOMEM error.
	 * Therefore, an array of pages is used to store an array of ranges of
	 * available disk space.
	 */
	ranges_buffer_size = karg.count * sizeof(struct blk_snap_block_range);
	ranges = big_buffer_alloc(ranges_buffer_size, GFP_KERNEL);
	if (!ranges) {
		pr_err("Unable to append difference storage: cannot allocate [%zu] bytes\n",
		       ranges_buffer_size);
		return -ENOMEM;
	}

	if (big_buffer_copy_from_user((void *)karg.ranges, 0, ranges,
				      ranges_buffer_size) !=
	    ranges_buffer_size) {
		pr_err("Unable to add file to snapstore: invalid user buffer for parameters\n");
		big_buffer_free(ranges);
		return -ENODATA;
	}

	res = snapshot_append_storage(&karg.id, karg.dev_id, ranges,
				      (size_t)karg.count);
	big_buffer_free(ranges);

	return res;
}

static int ioctl_snapshot_take(unsigned long arg)
{
	struct blk_snap_snapshot_take karg;

	if (copy_from_user(&karg, (void *)arg, sizeof(karg))) {
		pr_err("Unable to take snapshot: invalid user buffer\n");
		return -ENODATA;
	}

	return snapshot_take(&karg.id);
}

static int ioctl_snapshot_wait_event(unsigned long arg)
{
	int ret = 0;
	struct blk_snap_snapshot_event *karg;
	struct event *event;

	karg = kzalloc(sizeof(struct blk_snap_snapshot_event), GFP_KERNEL);
	if (!karg)
		return -ENOMEM;
	memory_object_inc(memory_object_blk_snap_snapshot_event);

	if (copy_from_user(karg, (void *)arg,
			   sizeof(struct blk_snap_snapshot_event))) {
		pr_err("Unable failed to get snapstore error code: invalid user buffer\n");
		ret = -EINVAL;
		goto out;
	}

	event = snapshot_wait_event(&karg->id, karg->timeout_ms);
	if (IS_ERR(event)) {
		ret = PTR_ERR(event);
		goto out;
	}

	pr_debug("Received event=%lld code=%d data_size=%d\n", event->time,
		 event->code, event->data_size);
	karg->code = event->code;
	karg->time_label = event->time;

	if (event->data_size > sizeof(karg->data)) {
		pr_err("Event size %d is too big\n", event->data_size);
		ret = -ENOSPC;
		/* If we can't copy all the data, we copy only part of it. */
	}
	memcpy(karg->data, event->data, event->data_size);
	event_free(event);

	if (copy_to_user((void *)arg, karg,
			 sizeof(struct blk_snap_snapshot_event))) {
		pr_err("Unable to get snapstore error code: invalid user buffer\n");
		ret = -EINVAL;
	}
out:
	kfree(karg);
	memory_object_dec(memory_object_blk_snap_snapshot_event);
	return ret;
}

static int ioctl_snapshot_collect(unsigned long arg)
{
	int ret;
	struct blk_snap_snapshot_collect karg;

	if (copy_from_user(&karg, (void *)arg, sizeof(karg))) {
		pr_err("Unable to collect available snapshots: invalid user buffer\n");
		return -ENODATA;
	}

	ret = snapshot_collect(&karg.count, karg.ids);

	if (copy_to_user((void *)arg, &karg, sizeof(karg))) {
		pr_err("Unable to collect available snapshots: invalid user buffer\n");
		return -ENODATA;
	}

	return ret;
}

static int ioctl_snapshot_collect_images(unsigned long arg)
{
	int ret;
	struct blk_snap_snapshot_collect_images karg;

	if (copy_from_user(&karg, (void *)arg, sizeof(karg))) {
		pr_err("Unable to collect snapshot images: invalid user buffer\n");
		return -ENODATA;
	}

	ret = snapshot_collect_images(&karg.id, karg.image_info_array,
				      &karg.count);

	if (copy_to_user((void *)arg, &karg, sizeof(karg))) {
		pr_err("Unable to collect snapshot images: invalid user buffer\n");
		return -ENODATA;
	}

	return ret;
}

static int (*const blk_snap_ioctl_table[])(unsigned long arg) = {
	ioctl_version,
	ioctl_tracker_remove,
	ioctl_tracker_collect,
	ioctl_tracker_read_cbt_map,
	ioctl_tracker_mark_dirty_blocks,
	ioctl_snapshot_create,
	ioctl_snapshot_destroy,
	ioctl_snapshot_append_storage,
	ioctl_snapshot_take,
	ioctl_snapshot_collect,
	ioctl_snapshot_collect_images,
	ioctl_snapshot_wait_event,
};

static_assert(
	sizeof(blk_snap_ioctl_table) == (blk_snap_ioctl_end * sizeof(void *)),
	"The size of table blk_snap_ioctl_table does not match the enum blk_snap_ioctl.");

#ifdef BLK_SNAP_MODIFICATION

int ioctl_mod(unsigned long arg)
{
	if (copy_to_user((void *)arg, &modification, sizeof(modification))) {
		pr_err("Unable to get modification: invalid user buffer\n");
		return -ENODATA;
	}

	return 0;
}

int ioctl_setlog(unsigned long arg)
{
#ifdef BLK_SNAP_FILELOG
	struct blk_snap_setlog karg;
	char *filepath = NULL;

	if (copy_from_user(&karg, (void *)arg, sizeof(karg))) {
		pr_err("Unable to get log parameters: invalid user buffer\n");
		return -ENODATA;
	}

	/*
	 * logging can be disabled
	 * To do this, it is enough not to specify a logging file or set
	 * a negative logging level.
	 */
	if ((karg.level < 0) || !karg.filepath)
		return log_restart(-1, NULL);

	if (karg.filepath_size == 0) {
		pr_err("Invalid parameters. 'filepath_size' cannot be zero\n");
		return -EINVAL;
	}
	filepath = kzalloc(karg.filepath_size + 1, GFP_KERNEL);
	if (!filepath)
		return -ENOMEM;
	memory_object_inc(memory_object_log_filepath);

	if (copy_from_user(filepath, (void *)karg.filepath, karg.filepath_size)) {
		pr_err("Unable to get log filepath: invalid user buffer\n");

		kfree(filepath);
		memory_object_dec(memory_object_log_filepath);
		return -ENODATA;
	}

	return log_restart(karg.level, filepath);
#else
	return -ENOTTY;
#endif
}

static int ioctl_get_sector_state(unsigned long arg)
{
#ifdef BLK_SNAP_DEBUG_SECTOR_STATE
	int ret;
	struct blk_snap_get_sector_state karg;
	dev_t dev_id;

	if (copy_from_user(&karg, (void *)arg, sizeof(karg))) {
		pr_err("Unable to get sector state: invalid user buffer\n");
		return -ENODATA;
	}

	dev_id = MKDEV(karg.image_dev_id.mj, karg.image_dev_id.mn);
	ret = snapshot_get_chunk_state(dev_id, karg.sector, &karg.state);
	if (unlikely(ret)) {
		pr_err("Failed to get sector state: cannot get chunk state\n");
		return ret;
	}

	if (copy_to_user((void *)arg, &karg, sizeof(karg))) {
		pr_err("Unable to get sector state: invalid user buffer\n");
		return -ENODATA;
	}

	return ret;
#else
	return -ENOTTY;
#endif
}

static int (*const blk_snap_ioctl_table_mod[])(unsigned long arg) = {
	ioctl_mod,
	ioctl_setlog,
	ioctl_get_sector_state,
};
static_assert(
	sizeof(blk_snap_ioctl_table_mod) ==
		((blk_snap_ioctl_end_mod - IOCTL_MOD) * sizeof(void *)),
	"The size of table blk_snap_ioctl_table_mod does not match the enum blk_snap_ioctl.");
#endif

static long ctrl_unlocked_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	int nr = _IOC_NR(cmd);

	if (nr > (sizeof(blk_snap_ioctl_table) / sizeof(void *))) {
#ifdef BLK_SNAP_MODIFICATION
		if ((nr >= IOCTL_MOD) &&
		    (nr < (IOCTL_MOD + (sizeof(blk_snap_ioctl_table_mod) /
					sizeof(void *))))) {
			nr -= IOCTL_MOD;
			if (blk_snap_ioctl_table_mod[nr])
				return blk_snap_ioctl_table_mod[nr](arg);
		}
#endif
		return -ENOTTY;
	}

	if (!blk_snap_ioctl_table[nr])
		return -ENOTTY;

	return blk_snap_ioctl_table[nr](arg);
}
