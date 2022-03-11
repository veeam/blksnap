// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/blk_snap.h>
#ifdef CONFIG_BLK_SNAP_DEBUG_MEMORY_LEAK
#include "memory_checker.h"
#endif
#include "version.h"
#include "params.h"
#include "ctrl.h"
#include "sysfs.h"
#include "snapimage.h"
#include "snapshot.h"
#include "tracker.h"
#include "diff_io.h"

static int __init blk_snap_init(void)
{
	int result;

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

	result = ctrl_init();
	if (result)
		return result;

	result = sysfs_init();
	return result;
}

static void __exit blk_snap_exit(void)
{
	pr_info("Unloading module\n");

	diff_io_done();
	snapshot_done();
	snapimage_done();
	tracker_done();

	sysfs_done();
	ctrl_done();

	pr_info("Module was unloaded\n");
}

module_init(blk_snap_init);
module_exit(blk_snap_exit);

int tracking_block_minimum_shift = CONFIG_BLK_SNAP_TRACKING_BLOCK_MINIMUM_SHIFT;
int tracking_block_maximum_count = CONFIG_BLK_SNAP_TRACKING_BLOCK_MAXIMUM_COUNT;
int chunk_minimum_shift = CONFIG_BLK_SNAP_CHUNK_MINIMUM_SHIFT;
int chunk_maximum_count = CONFIG_BLK_SNAP_CHUNK_MAXIMUM_COUNT;
int chunk_maximum_in_cache = CONFIG_BLK_SNAP_CHUNK_MAXIMUM_IN_CACHE;
int free_diff_buffer_pool_size = CONFIG_BLK_SNAP_FREE_DIFF_BUFFER_POOL_SIZE;
int diff_storage_minimum = CONFIG_BLK_SNAP_DIFF_STORAGE_MINIMUM;

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
