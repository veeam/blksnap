// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/miscdevice.h>
#ifdef STANDALONE_BDEVFILTER
#include "blksnap.h"
#else
#include <uapi/linux/blksnap.h>
#endif
#include "memory_checker.h"
#include "snapimage.h"
#include "snapshot.h"
#include "tracker.h"
#include "diff_io.h"
#ifdef STANDALONE_BDEVFILTER
#include "version.h"
#include "log.h"
#endif

static_assert(sizeof(uuid_t) == sizeof(struct blk_snap_uuid),
	"Invalid size of struct blk_snap_uuid.");

#ifdef STANDALONE_BDEVFILTER
#pragma message("Standalone bdevfilter")
#endif
#ifdef HAVE_QC_SUBMIT_BIO_NOACCT
#pragma message("The blk_qc_t submit_bio_noacct(struct bio *) function was found.")
#endif
#ifdef HAVE_VOID_SUBMIT_BIO_NOACCT
#pragma message("The void submit_bio_noacct(struct bio *) function was found.")
#endif
#ifdef HAVE_SUPER_BLOCK_FREEZE
#pragma message("The freeze_bdev() and thaw_bdev() have struct super_block.")
#endif
#ifdef HAVE_BI_BDEV
#pragma message("The struct bio have pointer to struct block_device.")
#endif
#ifdef HAVE_BI_BDISK
#pragma message("The struct bio have pointer to struct gendisk.")
#endif
#ifdef HAVE_BDEV_NR_SECTORS
#pragma message("The bdev_nr_sectors() function was found.")
#endif
#ifdef HAVE_BLK_ALLOC_DISK
#pragma message("The blk_alloc_disk() function was found.")
#endif
#ifdef HAVE_BIO_MAX_PAGES
#pragma message("The BIO_MAX_PAGES define was found.")
#endif
#ifdef HAVE_ADD_DISK_RESULT
#pragma message("The function add_disk() has a return code.")
#endif
#ifdef HAVE_GENHD_H
#pragma message("The header file 'genhd.h' was found.")
#endif
#ifdef HAVE_BDEV_BIO_ALLOC
#pragma message("The function bio_alloc_bioset() has a parameter bdev.")
#endif
#ifdef HAVE_BLK_CLEANUP_DISK
#pragma message("The function blk_cleanup_disk() was found.")
#endif

/*
 * The power of 2 for minimum tracking block size.
 * If we make the tracking block size small, we will get detailed information
 * about the changes, but the size of the change tracker table will be too
 * large, which will lead to inefficient memory usage.
 */
int tracking_block_minimum_shift = 16;

/*
 * The maximum number of tracking blocks.
 * A table is created to store information about the status of all tracking
 * blocks in RAM. So, if the size of the tracking block is small, then the size
 * of the table turns out to be large and memory is consumed inefficiently.
 * As the size of the block device grows, the size of the tracking block
 * size should also grow. For this purpose, the limit of the maximum
 * number of block size is set.
 */
int tracking_block_maximum_count = 2097152;

/*
 * The power of 2 for minimum chunk size.
 * The size of the chunk depends on how much data will be copied to the
 * difference storage when at least one sector of the block device is changed.
 * If the size is small, then small I/O units will be generated, which will
 * reduce performance. Too large a chunk size will lead to inefficient use of
 * the difference storage.
 */
int chunk_minimum_shift = 18;

/*
 * The maximum number of chunks.
 * To store information about the state of all the chunks, a table is created
 * in RAM. So, if the size of the chunk is small, then the size of the table
 * turns out to be large and memory is consumed inefficiently.
 * As the size of the block device grows, the size of the chunk should also
 * grow. For this purpose, the maximum number of chunks is set.
 */
int chunk_maximum_count = 2097152;

/*
 * The maximum number of chunks in memory cache.
 * Since reading and writing to snapshots is performed in large chunks,
 * a cache is implemented to optimize reading small portions of data
 * from the snapshot image. As the number of chunks in the cache
 * increases, memory consumption also increases.
 * The minimum recommended value is four.
 */
int chunk_maximum_in_cache = 32;

/*
 * The size of the pool of preallocated difference buffers.
 * A buffer can be allocated for each chunk. After use, this buffer is not
 * released immediately, but is sent to the pool of free buffers.
 * However, if there are too many free buffers in the pool, then these free
 * buffers will be released immediately.
 */
int free_diff_buffer_pool_size = 128;

/*
 * The minimum allowable size of the difference storage in sectors.
 * The difference storage is a part of the disk space allocated for storing
 * snapshot data. If there is less free space in the storage than the minimum,
 * an event is generated about the lack of free space.
 */
int diff_storage_minimum = 2097152;

#ifdef STANDALONE_BDEVFILTER
static const struct blk_snap_version version = {
	.major = VERSION_MAJOR,
	.minor = VERSION_MINOR,
	.revision = VERSION_REVISION,
	.build = VERSION_BUILD,
};
#else
#define VERSION_STR "1.0.0.0"
static const struct blk_snap_version version = {
	.major = 1,
	.minor = 0,
	.revision = 0,
	.build = 0,
};
#endif

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
	if (!cbt_info)
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
	struct blk_snap_dev *dev_id_array = NULL;
	uuid_t new_id;

	if (copy_from_user(&karg, (void *)arg, sizeof(karg))) {
		pr_err("Unable to create snapshot: invalid user buffer\n");
		return -ENODATA;
	}

	dev_id_array =
		kcalloc(karg.count, sizeof(struct blk_snap_dev), GFP_KERNEL);
	if (dev_id_array == NULL) {
		pr_err("Unable to create snapshot: too many devices %d\n",
		       karg.count);
		return -ENOMEM;
	}
	memory_object_inc(memory_object_blk_snap_dev);

	if (copy_from_user(dev_id_array, (void *)karg.dev_id_array,
			   karg.count * sizeof(struct blk_snap_dev))) {
		pr_err("Unable to create snapshot: invalid user buffer\n");
		ret = -ENODATA;
		goto out;
	}

	ret = snapshot_create(dev_id_array, karg.count, &new_id);
	if (ret)
		goto out;

	export_uuid(karg.id.b, &new_id);
	if (copy_to_user((void *)arg, &karg, sizeof(karg))) {
		pr_err("Unable to create snapshot: invalid user buffer\n");
		ret = -ENODATA;
	}
out:
	kfree(dev_id_array);
	memory_object_dec(memory_object_blk_snap_dev);

	return ret;
}

static int ioctl_snapshot_destroy(unsigned long arg)
{
	struct blk_snap_snapshot_destroy karg;
	uuid_t id;

	if (copy_from_user(&karg, (void *)arg, sizeof(karg))) {
		pr_err("Unable to destroy snapshot: invalid user buffer\n");
		return -ENODATA;
	}

	import_uuid(&id, karg.id.b);
	return snapshot_destroy(&id);
}

static int ioctl_snapshot_append_storage(unsigned long arg)
{
	struct blk_snap_snapshot_append_storage karg;
	uuid_t id;

	pr_debug("Append difference storage\n");

	if (copy_from_user(&karg, (void *)arg, sizeof(karg))) {
		pr_err("Unable to append difference storage: invalid user buffer\n");
		return -EINVAL;
	}

	import_uuid(&id, karg.id.b);
	return snapshot_append_storage(&id, karg.dev_id, karg.ranges,
				       karg.count);
}

static int ioctl_snapshot_take(unsigned long arg)
{
	struct blk_snap_snapshot_take karg;
	uuid_t id;

	if (copy_from_user(&karg, (void *)arg, sizeof(karg))) {
		pr_err("Unable to take snapshot: invalid user buffer\n");
		return -ENODATA;
	}

	import_uuid(&id, karg.id.b);
	return snapshot_take(&id);
}

static int ioctl_snapshot_wait_event(unsigned long arg)
{
	int ret = 0;
	struct blk_snap_snapshot_event *karg;
	uuid_t id;
	struct event *event;

	karg = kzalloc(sizeof(struct blk_snap_snapshot_event), GFP_KERNEL);
	if (!karg)
		return -ENOMEM;
	memory_object_inc(memory_object_blk_snap_snapshot_event);

	/* Copy only snapshot ID */
	if (copy_from_user(&karg->id,
			   &((struct blk_snap_snapshot_event *)arg)->id,
			   sizeof(struct blk_snap_uuid))) {
		pr_err("Unable to get snapshot event. Invalid user buffer\n");
		ret = -EINVAL;
		goto out;
	}

	import_uuid(&id, karg->id.b);
	event = snapshot_wait_event(&id, karg->timeout_ms);
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
		pr_err("Unable to get snapshot event. Invalid user buffer\n");
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
	uuid_t id;

	if (copy_from_user(&karg, (void *)arg, sizeof(karg))) {
		pr_err("Unable to collect snapshot images: invalid user buffer\n");
		return -ENODATA;
	}

	import_uuid(&id, karg.id.b);
	ret = snapshot_collect_images(&id, karg.image_info_array,
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
	sizeof(blk_snap_ioctl_table) ==
	((blk_snap_ioctl_snapshot_wait_event + 1) * sizeof(void *)),
	"The size of table blk_snap_ioctl_table does not match the enum blk_snap_ioctl.");

#ifdef BLK_SNAP_MODIFICATION

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
		return log_restart(-1, NULL, 0);

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

	return log_restart(karg.level, filepath, karg.tz_minuteswest);
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
#endif /*BLK_SNAP_MODIFICATION*/

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

static const struct file_operations blksnap_ctrl_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= ctrl_unlocked_ioctl,
};

static struct miscdevice blksnap_ctrl_misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= BLK_SNAP_CTL,
	.fops		= &blksnap_ctrl_fops,
};

static int __init blk_snap_init(void)
{
	int ret;

#ifdef BLK_SNAP_FILELOG
	log_init();
	pr_info("Loading\n");
#else
	pr_debug("Loading\n");
#endif
	pr_debug("Version: %s\n", VERSION_STR);
	pr_debug("tracking_block_minimum_shift: %d\n",
		 tracking_block_minimum_shift);
	pr_debug("tracking_block_maximum_count: %d\n",
		 tracking_block_maximum_count);
	pr_debug("chunk_minimum_shift: %d\n", chunk_minimum_shift);
	pr_debug("chunk_maximum_count: %d\n", chunk_maximum_count);
	pr_debug("chunk_maximum_in_cache: %d\n", chunk_maximum_in_cache);
	pr_debug("free_diff_buffer_pool_size: %d\n",
		 free_diff_buffer_pool_size);
	pr_debug("diff_storage_minimum: %d\n", diff_storage_minimum);

	ret = diff_io_init();
	if (ret)
		goto fail_diff_io_init;

	ret = snapimage_init();
	if (ret)
		goto fail_snapimage_init;

	ret = tracker_init();
	if (ret)
		goto fail_tracker_init;

	ret = misc_register(&blksnap_ctrl_misc);
	if (ret)
		goto fail_misc_register;

	return 0;

fail_misc_register:
	tracker_done();
fail_tracker_init:
	snapimage_done();
fail_snapimage_init:
	diff_io_done();
fail_diff_io_init:
#ifdef BLK_SNAP_FILELOG
	log_done();
#endif

	return ret;
}

static void __exit blk_snap_exit(void)
{
#ifdef BLK_SNAP_FILELOG
	pr_info("Unloading module\n");
#else
	pr_debug("Unloading module\n");
#endif
	misc_deregister(&blksnap_ctrl_misc);

	diff_io_done();
	snapshot_done();
	snapimage_done();
	tracker_done();

#ifdef BLK_SNAP_FILELOG
	log_done();
#endif
#ifdef BLK_SNAP_DEBUG_MEMORY_LEAK
	memory_object_print(true);
#endif
	pr_debug("Module was unloaded\n");
}

module_init(blk_snap_init);
module_exit(blk_snap_exit);

module_param_named(tracking_block_minimum_shift, tracking_block_minimum_shift,
		   int, 0644);
MODULE_PARM_DESC(tracking_block_minimum_shift,
		 "The power of 2 for minimum tracking block size");
module_param_named(tracking_block_maximum_count, tracking_block_maximum_count,
		   int, 0644);
MODULE_PARM_DESC(tracking_block_maximum_count,
		 "The maximum number of tracking blocks");
module_param_named(chunk_minimum_shift, chunk_minimum_shift, int, 0644);
MODULE_PARM_DESC(chunk_minimum_shift,
		 "The power of 2 for minimum chunk size");
module_param_named(chunk_maximum_count, chunk_maximum_count, int, 0644);
MODULE_PARM_DESC(chunk_maximum_count,
		 "The maximum number of chunks");
module_param_named(chunk_maximum_in_cache, chunk_maximum_in_cache, int, 0644);
MODULE_PARM_DESC(chunk_maximum_in_cache,
		 "The maximum number of chunks in memory cache");
module_param_named(free_diff_buffer_pool_size, free_diff_buffer_pool_size, int,
		   0644);
MODULE_PARM_DESC(free_diff_buffer_pool_size,
		 "The size of the pool of preallocated difference buffers");
module_param_named(diff_storage_minimum, diff_storage_minimum, int, 0644);
MODULE_PARM_DESC(diff_storage_minimum,
	"The minimum allowable size of the difference storage in sectors");

MODULE_DESCRIPTION("Block Device Snapshots Module");
MODULE_VERSION(VERSION_STR);
MODULE_AUTHOR("Veeam Software Group GmbH");
MODULE_LICENSE("GPL");

#ifdef STANDALONE_BDEVFILTER
/*
 * Allow to be loaded on OpenSUSE/SLES
 */
MODULE_INFO(supported, "external");
#endif
