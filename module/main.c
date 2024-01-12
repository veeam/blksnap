// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#ifdef STANDALONE_BDEVFILTER
#include "blk_snap.h"
#else
#include <linux/blk_snap.h>
#endif
#include "memory_checker.h"
#include "version.h"
#include "params.h"
#include "ctrl.h"
#include "sysfs.h"
#include "snapimage.h"
#include "snapshot.h"
#include "tracker.h"
#include "diff_io.h"
#ifdef STANDALONE_BDEVFILTER
#include "log.h"
#endif

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
#ifdef HAVE_BLK_MQ_ALLOC_DISK
#pragma message("The blk_mq_alloc_disk() function was found.")
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
#ifdef HAVE_BLK_HOLDER_OPS
#pragma message("The struct blk_holder_ops was found.")
#endif
#ifdef HAVE_BDEV_HANDLE
#pragma message("The struct bdev_handle was found.")
#endif
#ifdef HAVE_BDEV_FREEZE
#pragma message("The function bdev_freeze() was found.")
#endif
static int __init parameters_init(void);

static int __init blk_snap_init(void)
{
	int result;

#ifdef BLK_SNAP_FILELOG
	log_init();
#endif
	pr_info("Loading\n");
	pr_debug("Version: %s\n", VERSION_STR);
	result = parameters_init();
	if (result)
		return result;

	result = diff_io_init();
	if (result)
		return result;

	result = snapimage_init();
	if (result)
		return result;

	result = tracker_init();
	if (result)
		return result;

	result = ctrl_init();
	if (result)
		return result;

	result = sysfs_init();
	return result;
}

static void __exit blk_snap_exit(void)
{
	pr_info("Unloading module\n");

	sysfs_done();
	ctrl_done();

	diff_io_done();
	snapshot_done();
	snapimage_done();
	tracker_done();

#ifdef BLK_SNAP_FILELOG
	log_done();
#endif
#ifdef CONFIG_BLK_SNAP_DEBUG_MEMORY_LEAK
	memory_object_print(true);
#endif
	pr_info("Module was unloaded\n");
}

module_init(blk_snap_init);
module_exit(blk_snap_exit);

int tracking_block_minimum_shift = CONFIG_BLK_SNAP_TRACKING_BLOCK_MINIMUM_SHIFT;
int tracking_block_maximum_shift = 26; /* 2^26=64MiB - upper limit for a CBT block size */
int tracking_block_maximum_count = CONFIG_BLK_SNAP_TRACKING_BLOCK_MAXIMUM_COUNT;
int chunk_minimum_shift = CONFIG_BLK_SNAP_CHUNK_MINIMUM_SHIFT;
int chunk_maximum_shift = 26; /* 2^26=64MiB - upper limit for a chunk size */
int chunk_maximum_count_shift = 40; /*2^40= 1*10^12 - upper limit for a count of chunks*/
int chunk_maximum_in_cache = CONFIG_BLK_SNAP_CHUNK_MAXIMUM_IN_CACHE;
int free_diff_buffer_pool_size = CONFIG_BLK_SNAP_FREE_DIFF_BUFFER_POOL_SIZE;
int diff_storage_minimum = CONFIG_BLK_SNAP_DIFF_STORAGE_MINIMUM;

unsigned long get_chunk_maximum_count(void)
{
	return (1ul << chunk_maximum_count_shift);
}

static int __init parameters_init(void)
{
	pr_debug("tracking_block_minimum_shift: %d\n",
		 tracking_block_minimum_shift);
	pr_debug("tracking_block_maximum_count: %d\n",
		 tracking_block_maximum_count);
	pr_debug("chunk_minimum_shift: %d\n", chunk_minimum_shift);
	pr_debug("chunk_maximum_count_shift: %d\n", chunk_maximum_count_shift);
	pr_debug("chunk_maximum_in_cache: %d\n", chunk_maximum_in_cache);
	pr_debug("free_diff_buffer_pool_size: %d\n",
		 free_diff_buffer_pool_size);
	pr_debug("diff_storage_minimum: %d\n", diff_storage_minimum);

	if (chunk_maximum_shift < chunk_minimum_shift) {
		chunk_maximum_shift = chunk_minimum_shift;
		pr_warn("fixed chunk_maximum_shift: %d\n",
			 chunk_maximum_shift);
	}
	if (tracking_block_maximum_shift < tracking_block_minimum_shift) {
		tracking_block_maximum_shift = tracking_block_minimum_shift;
		pr_warn("fixed tracking_block_maximum_shift: %d\n",
			 tracking_block_maximum_shift);
	}

	/*
	 * The XArray is used to store chunks. And 'unsigned long' is used as
	 * chunk number parameter. So, The number of chunks cannot exceed the
	 * limits of ULONG_MAX.
	 */
	if (sizeof(unsigned long) < 4)
		chunk_maximum_count_shift = min(16, chunk_maximum_count_shift);
	else if (sizeof(unsigned long) == 4)
		chunk_maximum_count_shift = min(32, chunk_maximum_count_shift);
	else if (sizeof(unsigned long) >= 8)
		chunk_maximum_count_shift = min(64, chunk_maximum_count_shift);

	return 0;
}

module_param_named(tracking_block_minimum_shift, tracking_block_minimum_shift,
		   int, 0644);
MODULE_PARM_DESC(tracking_block_minimum_shift,
		 "The power of 2 for minimum trackings block size");
module_param_named(tracking_block_maximum_shift, tracking_block_maximum_shift,
		   int, 0644);
MODULE_PARM_DESC(tracking_block_maximum_shift,
		 "The power of 2 for maximum trackings block size");
module_param_named(tracking_block_maximum_count, tracking_block_maximum_count,
		   int, 0644);
MODULE_PARM_DESC(tracking_block_maximum_count,
		 "The limit of the maximum number of trackings blocks");
module_param_named(chunk_minimum_shift, chunk_minimum_shift, int, 0644);
MODULE_PARM_DESC(chunk_minimum_shift,
		 "The power of 2 for minimum snapshots chunk size");
module_param_named(chunk_maximum_shift, chunk_maximum_shift, int, 0644);
MODULE_PARM_DESC(chunk_maximum_shift,
		 "The power of 2 for maximum snapshots chunk size");
module_param_named(chunk_maximum_count_shift, chunk_maximum_count_shift, int, 0644);
MODULE_PARM_DESC(chunk_maximum_count_shift,
		 "The power of 2 for the maximum number of snapshots chunks");
module_param_named(chunk_maximum_in_cache, chunk_maximum_in_cache, int, 0644);
MODULE_PARM_DESC(chunk_maximum_in_cache,
		 "The limit of the maximum chunks in memory cache");
module_param_named(free_diff_buffer_pool_size, free_diff_buffer_pool_size, int,
		   0644);
MODULE_PARM_DESC(free_diff_buffer_pool_size,
		 "The maximum size of the free buffers pool");
module_param_named(diff_storage_minimum, diff_storage_minimum, int, 0644);
MODULE_PARM_DESC(
	diff_storage_minimum,
	"The minimum allowable size of the difference storage in sectors");

MODULE_DESCRIPTION("Block Layer Snapshot Kernel Module");
MODULE_VERSION(VERSION_STR);
MODULE_AUTHOR("Veeam Software Group GmbH");
MODULE_LICENSE("GPL");

#ifdef STANDALONE_BDEVFILTER
/*
 * Allow to be loaded on OpenSUSE/SLES
 */
MODULE_INFO(supported, "external");
#endif
