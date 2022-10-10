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

static int __init blk_snap_init(void)
{
	int result;

#ifdef BLK_SNAP_FILELOG
	log_init();
#endif
	pr_info("Loading\n");
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
#ifdef BLK_SNAP_DEBUG_MEMORY_LEAK
	memory_object_print(true);
#endif
	pr_info("Module was unloaded\n");
}

module_init(blk_snap_init);
module_exit(blk_snap_exit);

/*
 * The power of 2 for minimum tracking block size.
 * The minimum tracking block size by default is 64 KB (shift 16)
 * It's looks good for block device 128 GB or lower.
 * In this case, the block device is divided into 2097152 blocks.
 */
int tracking_block_minimum_shift = 16;

/*
 * The limit of the maximum number of tracking blocks.
 * As the size of the block device grows, the size of the tracking block
 * size should also grow. For this purpose, the limit of the maximum
 * number of block size is set.
 */
int tracking_block_maximum_count = 2097152;

/*
 * The minimum chunk size is 256 KB (shift 18).
 * It's looks good for block device 128 GB or lower.
 * In this case, the block device is divided into 524288 chunks.
 */
int chunk_minimum_shift = 18;

/*
 * As the size of the block device grows, the size of the chunk should also
 * grow. For this purpose, the limit of the maximum number of chunks is set.
 */
int chunk_maximum_count = 2097152;

/*
 * Since reading and writing to snapshots is performed in large chunks,
 * a cache is implemented to optimize reading small portions of data
 * from the snapshot image. As the number of chunks in the cache
 * increases, memory consumption also increases.
 * The minimum recommended value is four.
 */
int chunk_maximum_in_cache = 32;

/*
 * A buffer can be allocated for each chunk. After use, this buffer is not
 * released immediately, but is sent to the pool of free buffers.
 * However, if there are too many free buffers in the pool, they are released
 * immediately. The maximum size of the pool is regulated by this define.
 */
int free_diff_buffer_pool_size = 128;

/*
 * The minimum allowable size of the difference storage in sectors.
 * When reached, an event is generated about the lack of free space.
 */
int diff_storage_minimum = 2097152;

module_param_named(tracking_block_minimum_shift, tracking_block_minimum_shift,
		   int, 0644);
MODULE_PARM_DESC(tracking_block_minimum_shift,
		 "The power of 2 for minimum trackings block size");
module_param_named(tracking_block_maximum_count, tracking_block_maximum_count,
		   int, 0644);
MODULE_PARM_DESC(tracking_block_maximum_count,
		 "The limit of the maximum number of trackings blocks");
module_param_named(chunk_minimum_shift, chunk_minimum_shift, int, 0644);
MODULE_PARM_DESC(chunk_minimum_shift,
		 "The power of 2 for minimum snapshots chunk size");
module_param_named(chunk_maximum_count, chunk_maximum_count, int, 0644);
MODULE_PARM_DESC(chunk_maximum_count,
		 "The limit of the maximum number of snapshots chunks");
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
