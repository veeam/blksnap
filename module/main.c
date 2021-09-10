// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
//#include <linux/blk_types.h>
//#include <linux/blkdev.h>
//#include <linux/mm.h>
//#include <linux/rwsem.h>
//#include <linux/atomic.h>

#include "version.h"
#include "params.h"
#include "ctrl.h"
#include "sysfs.h"
#include "snapimage.h"
#include "snapshot.h"
#include "tracker.h"

static int __init blk_snap_init(void)
{
	int result;

	pr_info("Loading\n");

	result = ctrl_init();
	if (result)
		return result;

	result = snapimage_init();
	if (result)
		return result;

	result = sysfs_init();
	if (result)
		return result;

	result = tracker_init();

	return result;
}

/*
 * For standalone only:
 *
 * Before unload module livepatch should be detached.
 * echo 0 > /sys/kernel/livepatch/blk_snap_lp/enabled
 */
static void __exit blk_snap_exit(void)
{
	pr_info("Unloading module\n");

	tracker_done();
	snapshot_done();
	snapstore_done();
	snapimage_done();

	sysfs_done();
	ctrl_done();
}

module_init(blk_snap_init);
module_exit(blk_snap_exit);

int tracking_block_minimum_shift = CONFIG_BLK_SNAP_TRACKING_BLOCK_MINIMUM_SHIFT;
int tracking_block_maximum_count = CONFIG_BLK_SNAP_TRACKING_BLOCK_MAXIMUM_COUNT;
int chunk_minimum_shift = CONFIG_BLK_SNAP_CHUNK_MINIMUM_SHIFT;
int chunk_maximum_count = CONFIG_BLK_SNAP_CHUNK_MAXIMUM_COUNT;
int chunk_maximum_in_cache = CONFIG_BLK_SNAP_CHUNK_MAXIMUM_IN_CACHE;
int diff_storage_minimum = CONFIG_BLK_SNAP_DIFF_STORAGE_MINIMUM;

module_param_named(tracking_block_minimum_shift, tracking_block_minimum_shift, int, 0644);
MODULE_PARM_DESC(tracking_block_minimum_shift,
		 "The power of 2 for minimum trackings block size");
module_param_named(tracking_block_maximum_count, tracking_block_maximum_count, int, 0644);
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
module_param_named(diff_storage_minimum, diff_storage_minimum, int, 0644);
MODULE_PARM_DESC(diff_storage_minimum,
		 "The minimum allowable size of the difference storage in sectors");

MODULE_DESCRIPTION("Block Layer Snapshot Kernel Module");
MODULE_VERSION(FILEVER_STR);
MODULE_AUTHOR("Veeam Software Group GmbH");
MODULE_LICENSE("GPL");
MODULE_INFO(livepatch, "Y");

