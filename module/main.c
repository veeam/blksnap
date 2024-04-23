// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Veeam Software Group GmbH */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/build_bug.h>
#include <linux/slab.h>
#ifdef BLKSNAP_STANDALONE
#include "veeamblksnap.h"
#include "version.h"
#else
#include <uapi/linux/blksnap.h>
#endif
#include "snapimage.h"
#include "snapshot.h"
#include "tracker.h"
#include "chunk.h"
#include "params.h"
#ifdef BLKSNAP_FILELOG
#include "log.h"
#endif
#ifdef BLKSNAP_MEMSTAT
#include "memstat.h"
#endif
#ifdef BLKSNAP_HISTOGRAM
#include "log_histogram.h"
#endif
/*
 * The power of 2 for minimum tracking block size.
 *
 * If we make the tracking block size small, we will get detailed information
 * about the changes, but the size of the change tracker table will be too
 * large, which will lead to inefficient memory usage.
 */
static unsigned int tracking_block_minimum_shift = 16;

/*
 * The maximum number of tracking blocks.
 *
 * A table is created in RAM to store information about the status of all
 * tracking blocks. So, if the size of the tracking block is small, then the
 * size of the table turns out to be large and memory is consumed inefficiently.
 * As the size of the block device grows, the size of the tracking block size
 * should also grow. For this purpose, the limit of the maximum number of block
 * size is set.
 */
static unsigned int tracking_block_maximum_count = 2097152;

/*
 * The power of 2 for maximum tracking block size.
 *
 * On very large capacity disks, the block size may be too large. To prevent
 * this, the maximum block size is limited. If the limit on the maximum block
 * size has been reached, then the number of blocks may exceed the
 * &tracking_block_maximum_count.
 */
static unsigned int tracking_block_maximum_shift = 26;

/*
 * The power of 2 for minimum chunk size.
 *
 * The size of the chunk depends on how much data will be copied to the
 * difference storage when at least one sector of the block device is changed.
 * If the size is small, then small I/O units will be generated, which will
 * reduce performance. Too large a chunk size will lead to inefficient use of
 * the difference storage.
 */
static unsigned int chunk_minimum_shift = 18;

/*
 * The power of 2 for maximum number of chunks.
 *
 * A table is created in RAM to store information about the state of the chunks.
 * So, if the size of the chunk is small, then the size of the table turns out
 * to be large and memory is consumed inefficiently. As the size of the block
 * device grows, the size of the chunk should also grow. For this purpose, the
 * maximum number of chunks is set.
 *
 * The table expands dynamically when new chunks are allocated. Therefore,
 * memory consumption also depends on the intensity of writing to the block
 * device under the snapshot.
 */
static unsigned int chunk_maximum_count_shift = 40;

/*
 * The power of 2 for maximum chunk size.
 *
 * On very large capacity disks, the chunk size may be too large. To prevent
 * this, the maximum block size is limited. If the limit on the maximum chunk
 * size has been reached, then the number of chunks may exceed the
 * &chunk_maximum_count.
 */
static unsigned int chunk_maximum_shift = 26;

/*
 * The maximum number of chunks in queue.
 *
 * The chunk is not immediately stored to the difference storage. The chunks
 * are put in a store queue. The store queue allows to postpone the operation
 * of storing a chunks data to the difference storage and perform it later in
 * the worker thread.
 */
static unsigned int chunk_maximum_in_queue = 16;

/*
 * The size of the pool of preallocated difference buffers.
 *
 * A buffer can be allocated for each chunk. After use, this buffer is not
 * released immediately, but is sent to the pool of free buffers. However, if
 * there are too many free buffers in the pool, then these free buffers will
 * be released immediately.
 */
static unsigned int free_diff_buffer_pool_size = 128;

/*
 * The minimum allowable size of the difference storage in sectors.
 *
 * The difference storage is a part of the disk space allocated for storing
 * snapshot data. If the free space in difference storage is less than half of
 * this value, then the process of increasing the size of the difference storage
 * file will begin. The size of the difference storage file is increased in
 * portions, the size of which is determined by this value.
 */
static unsigned int diff_storage_minimum = 2097152;

#if defined(BLKSNAP_STANDALONE)
static const struct blksnap_version version = {
	.major = VERSION_MAJOR,
	.minor = VERSION_MINOR,
	.revision = VERSION_REVISION,
	.build = VERSION_BUILD,
};
#else
#define VERSION_STR "2.0.0.0"
static const struct blksnap_version version = {
	.major = 2,
	.minor = 0,
	.revision = 0,
	.build = 0,
};
#endif

#ifdef BLKSNAP_MODIFICATION
static const struct blksnap_mod modification = {
	.name = "standalone",
	.compatibility_flags =
#ifdef BLKSNAP_DEBUG_SECTOR_STATE
	(1ull << blksnap_compat_flag_debug_sector_state) |
#endif
#ifdef BLKSNAP_FILELOG
	(1ull << blksnap_compat_flag_setlog) |
#endif
	0
};
#endif

static struct workqueue_struct *blksnap_wq;

unsigned int get_tracking_block_minimum_shift(void)
{
	return tracking_block_minimum_shift;
}

unsigned int get_tracking_block_maximum_shift(void)
{
	return tracking_block_maximum_shift;
}

unsigned int get_tracking_block_maximum_count(void)
{
	return tracking_block_maximum_count;
}

unsigned int get_chunk_minimum_shift(void)
{
	return chunk_minimum_shift;
}

unsigned int get_chunk_maximum_shift(void)
{
	return chunk_maximum_shift;
}

unsigned long get_chunk_maximum_count(void)
{
	/*
	 * The XArray is used to store chunks. And 'unsigned long' is used as
	 * chunk number parameter. So, The number of chunks cannot exceed the
	 * limits of ULONG_MAX.
	 */
	if ((chunk_maximum_count_shift >> 3) < sizeof(unsigned long))
		return (1ul << chunk_maximum_count_shift);
	return ULONG_MAX;
}

unsigned int get_chunk_maximum_in_queue(void)
{
	return chunk_maximum_in_queue;
}

unsigned int get_free_diff_buffer_pool_size(void)
{
	return free_diff_buffer_pool_size;
}

sector_t get_diff_storage_minimum(void)
{
	return (sector_t)diff_storage_minimum;
}

bool blksnap_queue_work(struct work_struct *work)
{
	return queue_work(blksnap_wq, work);
}

static int ioctl_version(struct blksnap_version __user *user_version)
{
	if (copy_to_user(user_version, &version, sizeof(version))) {
		pr_err("Unable to get version: invalid user buffer\n");
		return -ENODATA;
	}

	return 0;
}

static_assert(sizeof(uuid_t) == sizeof(struct blksnap_uuid),
	"Invalid size of struct blksnap_uuid.");

static int ioctl_snapshot_create(struct blksnap_snapshot_create __user *uarg)
{
	struct blksnap_snapshot_create karg;
	char *fname;
	int ret;

	if (copy_from_user(&karg, uarg, sizeof(karg))) {
		pr_err("Unable to create snapshot: invalid user buffer\n");
		return -ENODATA;
	}

	if (karg.diff_storage_filename) {
		fname = strndup_user((const char __user *)karg.diff_storage_filename,
				     PATH_MAX);
		if (IS_ERR(fname))
			return PTR_ERR(fname);
	} else {
#if defined(BLKSNAP_MODIFICATION)
		fname = NULL;
#else
		pr_err("Unable to create snapshot: difference storage file is not set\n");
		return -EINVAL;
#endif
	}
	ret = snapshot_create(fname, karg.diff_storage_limit_sect, &karg.id);
	kfree(fname);
#ifdef BLKSNAP_MEMSTAT
	// fname has been created by strndup_user()
#endif
	if (ret)
		return ret;

	if (copy_to_user(uarg, &karg, sizeof(karg))) {
		snapshot_destroy((uuid_t *)&karg.id);
		pr_err("Unable to create snapshot: invalid user buffer\n");
		return -ENODATA;
	}

	return 0;
}

static int ioctl_snapshot_destroy(struct blksnap_uuid __user *user_id)
{
	uuid_t kernel_id;

	if (copy_from_user(kernel_id.b, user_id->b, sizeof(uuid_t))) {
		pr_err("Unable to destroy snapshot: invalid user buffer\n");
		return -ENODATA;
	}

	return snapshot_destroy(&kernel_id);
}

static int ioctl_snapshot_take(struct blksnap_uuid __user *user_id)
{
	uuid_t kernel_id;

	if (copy_from_user(kernel_id.b, user_id->b, sizeof(uuid_t))) {
		pr_err("Unable to take snapshot: invalid user buffer\n");
		return -ENODATA;
	}

	return snapshot_take(&kernel_id);
}

static int ioctl_snapshot_collect(struct blksnap_snapshot_collect __user *uarg)
{
	int ret;
	struct blksnap_snapshot_collect karg;

	if (copy_from_user(&karg, uarg, sizeof(karg))) {
		pr_err("Unable to collect available snapshots: invalid user buffer\n");
		return -ENODATA;
	}

	ret = snapshot_collect(&karg.count, u64_to_user_ptr(karg.ids));

	if (copy_to_user(uarg, &karg, sizeof(karg))) {
		pr_err("Unable to collect available snapshots: invalid user buffer\n");
		return -ENODATA;
	}

	return ret;
}

static_assert(sizeof(struct blksnap_snapshot_event) == 4096,
	"The size struct blksnap_snapshot_event should be equal to the size of the page.");

static int ioctl_snapshot_wait_event(struct blksnap_snapshot_event __user *uarg)
{
	int ret = 0;
	struct blksnap_snapshot_event *karg;
	struct event *ev;

#ifdef BLKSNAP_MEMSTAT
	karg = __kzalloc(sizeof(struct blksnap_snapshot_event), GFP_KERNEL);
#else
	karg = kzalloc(sizeof(struct blksnap_snapshot_event), GFP_KERNEL);
#endif
	if (!karg)
		return -ENOMEM;

	/* Copy only snapshot ID and timeout*/
	if (copy_from_user(karg, uarg, sizeof(uuid_t) + sizeof(__u32))) {
		pr_err("Unable to get snapshot event. Invalid user buffer\n");
		ret = -ENODATA;
		goto out;
	}

	ev = snapshot_wait_event((uuid_t *)karg->id.b, karg->timeout_ms);
	if (IS_ERR(ev)) {
		ret = PTR_ERR(ev);
		goto out;
	}

	pr_debug("Sent event: time=%lld code=%d data_size=%d\n", ev->time,
		 ev->code, ev->data_size);
	karg->code = ev->code;
	karg->time_label = ev->time;

	if (ev->data_size > sizeof(karg->data)) {
		pr_err("Event size %d is too big\n", ev->data_size);
		ret = -ENOSPC;
		/* If we can't copy all the data, we copy only part of it. */
	}
	memcpy(karg->data, ev->data, ev->data_size);
	event_free(ev);

	if (copy_to_user(uarg, karg, sizeof(struct blksnap_snapshot_event))) {
		pr_err("Unable to get snapshot event. Invalid user buffer\n");
		ret = -ENODATA;
	}
out:
#ifdef BLKSNAP_MEMSTAT
	__kfree(karg);
#else
	kfree(karg);
#endif
	return ret;
}

#ifdef BLKSNAP_MODIFICATION

static int ioctl_mod(struct blksnap_mod __user *uarg)
{
	if (copy_to_user(uarg, &modification, sizeof(modification))) {
		pr_err("Unable to get modification: invalid user buffer\n");
		return -ENODATA;
	}

	return 0;
}

static int ioctl_setlog(struct blksnap_setlog __user *uarg)
{
	int ret = -ENOTTY;
	struct blksnap_setlog karg;

	if (copy_from_user(&karg, uarg, sizeof(karg))) {
		pr_err("Unable to get log parameters: invalid user buffer\n");
		return -ENODATA;
	}
#ifdef BLKSNAP_MEMSTAT
	memstat_enable((int)(karg.level >= LOGLEVEL_DEBUG));
	ret = 0;
#endif
#ifdef BLKSNAP_HISTOGRAM
	log_histogram_enable((int)(karg.level >= LOGLEVEL_DEBUG));
	ret = 0;
#endif
#ifdef BLKSNAP_FILELOG
	{
		char *filepath = NULL;
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
#ifdef BLKSNAP_MEMSTAT
		// We do not take into account the memory that the logger uses,
		// since it frees up its resources after displaying statistics
		// on memory leaks.
#endif
		if (!filepath)
			return -ENOMEM;

		if (copy_from_user(filepath, (void *)karg.filepath,
				   karg.filepath_size)) {
			pr_err("Unable to get log filepath: invalid user buffer\n");

			kfree(filepath);
			return -ENODATA;
		}

		ret = log_restart(karg.level, filepath, karg.tz_minuteswest);
	}
#endif
	return ret;
}

static int ioctl_snapshot_append_storage(
	struct blksnap_snapshot_append_storage __user *uarg)
{
	int ret;
	struct blksnap_snapshot_append_storage karg;
	char *devpath;

	pr_debug("Append difference storage\n");

	if (copy_from_user(&karg, (void *)uarg, sizeof(karg))) {
		pr_err("Unable to append difference storage: invalid user buffer\n");
		return -ENODATA;
	}
	devpath = strndup_user((const char __user *)karg.devpath, PATH_MAX);
	if (IS_ERR(devpath))
		return PTR_ERR(devpath);

	pr_debug("Append difference storage on device '%s' with %d ranges\n",
		devpath, karg.count);
	ret = snapshot_append_storage((uuid_t *)karg.id.b, devpath,
	       (size_t)karg.count, (struct blksnap_sectors* __user)karg.ranges);

	kfree(devpath);
	return ret;
}
#endif /* BLKSNAP_MODIFICATION */

static long blksnap_ctrl_unlocked_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	void *argp = (void __user *)arg;

	switch (cmd) {
	case IOCTL_BLKSNAP_VERSION:
		return ioctl_version(argp);
	case IOCTL_BLKSNAP_SNAPSHOT_CREATE:
		return ioctl_snapshot_create(argp);
	case IOCTL_BLKSNAP_SNAPSHOT_DESTROY:
		return ioctl_snapshot_destroy(argp);
	case IOCTL_BLKSNAP_SNAPSHOT_TAKE:
		return ioctl_snapshot_take(argp);
	case IOCTL_BLKSNAP_SNAPSHOT_COLLECT:
		return ioctl_snapshot_collect(argp);
	case IOCTL_BLKSNAP_SNAPSHOT_WAIT_EVENT:
		return ioctl_snapshot_wait_event(argp);
#ifdef BLKSNAP_MODIFICATION
	case IOCTL_BLKSNAP_MOD:
		return ioctl_mod(argp);
	case IOCTL_BLKSNAP_SETLOG:
		return ioctl_setlog(argp);
	case IOCTL_BLKSNAP_SNAPSHOT_APPEND_STORAGE:
		return ioctl_snapshot_append_storage(argp);
#endif
	default:
		return -ENOTTY;
	}

}

static const struct file_operations blksnap_ctrl_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= blksnap_ctrl_unlocked_ioctl,
};

static struct miscdevice blksnap_ctrl_misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= BLKSNAP_CTL,
	.fops		= &blksnap_ctrl_fops,
};

static inline sector_t chunk_minimum_sectors(void)
{
	return (1ull << (chunk_minimum_shift - SECTOR_SHIFT));
}

static int __init parameters_init(void)
{
	pr_debug("tracking_block_minimum_shift: %d\n",
		 tracking_block_minimum_shift);
	pr_debug("tracking_block_maximum_shift: %d\n",
		 tracking_block_maximum_shift);
	pr_debug("tracking_block_maximum_count: %d\n",
		 tracking_block_maximum_count);

	pr_debug("chunk_minimum_shift: %d\n", chunk_minimum_shift);
	pr_debug("chunk_maximum_shift: %d\n", chunk_maximum_shift);
	pr_debug("chunk_maximum_count_shift: %u\n", chunk_maximum_count_shift);

	pr_debug("chunk_maximum_in_queue: %d\n", chunk_maximum_in_queue);
	pr_debug("free_diff_buffer_pool_size: %d\n",
		 free_diff_buffer_pool_size);
	pr_debug("diff_storage_minimum: %d\n", diff_storage_minimum);

	if (tracking_block_maximum_shift < tracking_block_minimum_shift) {
		tracking_block_maximum_shift = tracking_block_minimum_shift;
		pr_warn("fixed tracking_block_maximum_shift: %d\n",
			 tracking_block_maximum_shift);
	}

	if (chunk_minimum_shift > chunk_maximum_shift) {
		chunk_minimum_shift = chunk_maximum_shift;
		pr_warn("fixed chunk_minimum_shift: %d\n",
			 chunk_minimum_shift);
	}
	if (chunk_minimum_shift < PAGE_SHIFT) {
		chunk_minimum_shift = PAGE_SHIFT;
		pr_warn("fixed chunk_minimum_shift: %d\n",
			 chunk_minimum_shift);
	}
	if (diff_storage_minimum < (chunk_minimum_sectors() * 2)) {
		diff_storage_minimum = chunk_minimum_sectors() * 2;
		pr_warn("fixed diff_storage_minimum: %d\n",
			 diff_storage_minimum);
	}
	if (diff_storage_minimum & (chunk_minimum_sectors() - 1)) {
		diff_storage_minimum &= ~(chunk_minimum_sectors() - 1);
		pr_warn("fixed diff_storage_minimum: %d\n",
			 diff_storage_minimum);
	}
#ifdef CONFIG_64BIT
	chunk_maximum_count_shift = min((unsigned int)40,
					chunk_maximum_count_shift);
#else
	chunk_maximum_count_shift = min((unsigned int)32,
					chunk_maximum_count_shift);
#endif
	return 0;
}

static int __init blksnap_init(void)
{
	int ret;

#ifdef BLKSNAP_FILELOG
	log_init();
#endif
	pr_debug("Loading\n");
	pr_debug("Version: %s\n", VERSION_STR);

	ret = parameters_init();
	if (ret)
		return ret;

	ret = chunk_init();
	if (ret)
		goto fail_chunk_init;

	blksnap_wq = alloc_workqueue("blksnap",
				      WQ_UNBOUND | WQ_HIGHPRI | WQ_SYSFS, 0);
	if (!blksnap_wq) {
		ret = -ENOMEM;
		goto fail_wq_init;
	}

	ret = tracker_init();
	if (ret)
		goto fail_tracker_init;

#if !defined(HAVE_BLK_ALLOC_DISK)
	ret = snapimage_init();
	if (ret)
		goto fail_snapimage_init;
#endif
	ret = misc_register(&blksnap_ctrl_misc);
	if (ret)
		goto fail_misc_register;

	return 0;

fail_misc_register:
#if !defined(HAVE_BLK_ALLOC_DISK)
	snapimage_done();
fail_snapimage_init:
#endif
	tracker_done();
fail_tracker_init:
	destroy_workqueue(blksnap_wq);
fail_wq_init:
	chunk_done();
fail_chunk_init:

	return ret;
}

static void __exit blksnap_exit(void)
{
	pr_debug("Unloading module\n");

	misc_deregister(&blksnap_ctrl_misc);

	snapshot_done();
	tracker_done();
	destroy_workqueue(blksnap_wq);
	chunk_done();
#ifdef BLKSNAP_MEMSTAT
	memstat_done();
#endif
#ifdef BLKSNAP_FILELOG
	log_done();
#endif
	pr_debug("Module was unloaded\n");
}

module_init(blksnap_init);
module_exit(blksnap_exit);

module_param_named(tracking_block_minimum_shift, tracking_block_minimum_shift,
		   uint, 0644);
MODULE_PARM_DESC(tracking_block_minimum_shift,
		 "The power of 2 for minimum tracking block size");
module_param_named(tracking_block_maximum_count, tracking_block_maximum_count,
		   uint, 0644);
MODULE_PARM_DESC(tracking_block_maximum_count,
		 "The maximum number of tracking blocks");
module_param_named(tracking_block_maximum_shift, tracking_block_maximum_shift,
		   uint, 0644);
MODULE_PARM_DESC(tracking_block_maximum_shift,
		 "The power of 2 for maximum trackings block size");
module_param_named(chunk_minimum_shift, chunk_minimum_shift, uint, 0644);
MODULE_PARM_DESC(chunk_minimum_shift,
		 "The power of 2 for minimum chunk size");
module_param_named(chunk_maximum_count_shift, chunk_maximum_count_shift,
		   uint, 0644);
MODULE_PARM_DESC(chunk_maximum_count_shift,
		 "The power of 2 for maximum number of chunks");
module_param_named(chunk_maximum_shift, chunk_maximum_shift, uint, 0644);
MODULE_PARM_DESC(chunk_maximum_shift,
		 "The power of 2 for maximum snapshots chunk size");
module_param_named(chunk_maximum_in_queue, chunk_maximum_in_queue, uint, 0644);
MODULE_PARM_DESC(chunk_maximum_in_queue,
		 "The maximum number of chunks in store queue");
module_param_named(free_diff_buffer_pool_size, free_diff_buffer_pool_size,
		   uint, 0644);
MODULE_PARM_DESC(free_diff_buffer_pool_size,
		 "The size of the pool of preallocated difference buffers");
module_param_named(diff_storage_minimum, diff_storage_minimum, uint, 0644);
MODULE_PARM_DESC(diff_storage_minimum,
	"The minimum allowable size of the difference storage in sectors");

MODULE_DESCRIPTION("Block Device Snapshots Module");
MODULE_VERSION(VERSION_STR);
MODULE_AUTHOR("Veeam Software Group GmbH");
MODULE_LICENSE("GPL");
