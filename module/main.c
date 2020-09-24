// SPDX-License-Identifier: GPL-2.0
#include "common.h"
#include "version.h"
#include "blk-snap-ctl.h"
#include "ctrl_fops.h"
#include "ctrl_pipe.h"
#include "ctrl_sysfs.h"

#include "blk_redirect.h"
#include "blk_deferred.h"
#include "snapimage.h"

#include "snapstore.h"
#include "snapstore_device.h"

#include "snapshot.h"
#include "tracker.h"
#include "tracking.h"

#include <linux/init.h>
#include <linux/module.h>

#include <linux/notifier.h>
#include <linux/blk-filter.h>
#define BLK_SNAP_DEFAULT_ALTITUDE BLK_FILTER_ALTITUDE_MIN

#include <linux/syscore_ops.h> //more modern method

static int g_param_zerosnapdata = 0; /*rudiment */
static int g_param_debuglogging = 0; /*rudiment */
static unsigned int g_param_fixflags = 0; /*rudiment */

static char *g_logdir = NULL; /*rudiment */
static unsigned long g_param_logmaxsize = 15 * 1024 * 1024; /*rudiment */

static int g_param_snapstore_block_size_pow = 14;
static int g_param_change_tracking_block_size_pow = 18;

int get_debuglogging(void)
{
	return g_param_debuglogging;
}

int get_snapstore_block_size_pow(void)
{
	return g_param_snapstore_block_size_pow;
}
int inc_snapstore_block_size_pow(void)
{
	if (g_param_snapstore_block_size_pow > 30)
		return -EFAULT;

	++g_param_snapstore_block_size_pow;
	return SUCCESS;
}
int get_change_tracking_block_size_pow(void)
{
	return g_param_change_tracking_block_size_pow;
}

static int blk_snap_major = 0;

int get_blk_snap_major(void)
{
	return blk_snap_major;
}

blk_qc_t filter_submit_original_bio(struct bio *bio);

static void filter_disk_add(struct gendisk *disk)
{
	pr_info("new disk [%s] in system\n", disk->disk_name);
}
static void filter_disk_del(struct gendisk *disk)
{
	pr_info("del disk [%s] from system\n", disk->disk_name);
}
static void filter_disk_release(struct gendisk *disk)
{
	pr_info("release disk [%s] from system\n", disk->disk_name);
}
static blk_qc_t filter_submit_bio(struct bio *bio)
{
	blk_qc_t result;
	if (tracking_submit_bio(bio, &result))
		return result;
	else
		return filter_submit_original_bio(bio);
}

static const struct blk_filter_ops g_filter_ops = { .disk_add = filter_disk_add,
						    .disk_del = filter_disk_del,
						    .disk_release = filter_disk_release,
						    .submit_bio = filter_submit_bio };

static struct blk_filter g_filter = { .name = MODULE_NAME,
				      .ops = &g_filter_ops,
				      .altitude = BLK_SNAP_DEFAULT_ALTITUDE,
				      .blk_filter_ctx = NULL };

blk_qc_t filter_submit_original_bio(struct bio *bio)
{
	return blk_filter_submit_bio_next(&g_filter, bio);
}

static struct device *blk_snap_device = NULL;

static struct file_operations ctrl_fops = { .owner = THIS_MODULE,
					    .read = ctrl_read,
					    .write = ctrl_write,
					    .open = ctrl_open,
					    .release = ctrl_release,
					    .poll = ctrl_poll,
					    .unlocked_ioctl = ctrl_unlocked_ioctl };

static void blk_snap_syscore_shutdown(void)
{
	tracker_remove_all();
}

struct syscore_ops blk_snap_syscore_ops = {
	.node = { 0 },
	.suspend = NULL,
	.resume = NULL,
	.shutdown = blk_snap_syscore_shutdown,
};

int __init blk_snap_init(void)
{
	int result = SUCCESS;

	pr_info("Loading\n");
	pr_info("snapstore_block_size_pow: %d\n", g_param_snapstore_block_size_pow);
	pr_info("change_tracking_block_size_pow: %d\n", g_param_change_tracking_block_size_pow);

	if (g_param_snapstore_block_size_pow > 23) {
		g_param_snapstore_block_size_pow = 23;
		pr_info("Limited snapstore_block_size_pow: %d\n", g_param_snapstore_block_size_pow);
	} else if (g_param_snapstore_block_size_pow < 12) {
		g_param_snapstore_block_size_pow = 12;
		pr_info("Limited snapstore_block_size_pow: %d\n", g_param_snapstore_block_size_pow);
	}

	if (g_param_change_tracking_block_size_pow > 23) {
		g_param_change_tracking_block_size_pow = 23;
		pr_info("Limited change_tracking_block_size_pow: %d\n",
			g_param_change_tracking_block_size_pow);
	} else if (g_param_change_tracking_block_size_pow < 12) {
		g_param_change_tracking_block_size_pow = 12;
		pr_info("Limited change_tracking_block_size_pow: %d\n",
			g_param_change_tracking_block_size_pow);
	}

	do {
		pr_info("Registering reboot notification\n");

		register_syscore_ops(&blk_snap_syscore_ops);

		blk_snap_major = register_chrdev(0, MODULE_NAME, &ctrl_fops);
		if (blk_snap_major < 0) {
			pr_err("Failed to register a character device. errno=%d\n", blk_snap_major);
			result = blk_snap_major;
			break;
		}
		pr_info("Module major [%d]\n", blk_snap_major);

		if ((result = blk_redirect_bioset_create()) != SUCCESS)
			break;

		if ((result = blk_deferred_bioset_create()) != SUCCESS)
			break;

		if ((result = snapimage_init()) != SUCCESS)
			break;

		if ((result = ctrl_sysfs_init(&blk_snap_device)) != SUCCESS) {
			pr_err("Failed to initialize sysfs attributes\n");
			break;
		}

		if ((result = blk_filter_register(&g_filter)) != SUCCESS) {
			const char *exist_filter = blk_filter_check_altitude(g_filter.altitude);
			if (exist_filter)
				pr_err("Block io layer filter [%s] already exist on altitude [%ld]\n",
				       exist_filter, g_filter.altitude);

			pr_err("Failed to register block io layer filter\n");
			break;
		}

	} while (false);

	return result;
}

void __exit blk_snap_exit(void)
{
	int result;
	pr_info("Unloading module\n");

	unregister_syscore_ops(&blk_snap_syscore_ops);

	ctrl_sysfs_done(&blk_snap_device);

	snapshot_done();

	snapstore_device_done();
	snapstore_done();

	tracker_done();

	result = blk_filter_unregister(&g_filter);
	BUG_ON(result);

	snapimage_done();

	blk_deferred_bioset_free();
	blk_deferred_done();

	blk_redirect_bioset_free();

	unregister_chrdev(blk_snap_major, MODULE_NAME);

	ctrl_done();
}

module_init(blk_snap_init);
module_exit(blk_snap_exit);

module_param_named(zerosnapdata, g_param_zerosnapdata, int, 0644);
MODULE_PARM_DESC(zerosnapdata, "Zeroing snapshot data algorithm determine.");

module_param_named(debuglogging, g_param_debuglogging, int, 0644);
MODULE_PARM_DESC(debuglogging, "Logging level switch.");

module_param_named(logdir, g_logdir, charp, 0644);
MODULE_PARM_DESC(logdir, "Directory for module logs.");

module_param_named(logmaxsize, g_param_logmaxsize, ulong, 0644);
MODULE_PARM_DESC(logmaxsize, "Maximum log file size.");

module_param_named(snapstore_block_size_pow, g_param_snapstore_block_size_pow, int, 0644);
MODULE_PARM_DESC(snapstore_block_size_pow,
		 "Snapstore block size binary pow. 20 for 1MiB block size");

module_param_named(change_tracking_block_size_pow, g_param_change_tracking_block_size_pow, int,
		   0644);
MODULE_PARM_DESC(change_tracking_block_size_pow,
		 "Change-tracking block size binary pow. 18 for 256 KiB block size");

module_param_named(fixflags, g_param_fixflags, uint, 0644);
MODULE_PARM_DESC(fixflags, "Flags for known issues");

MODULE_DESCRIPTION("Block Layer Snapshot Kernel Module");
MODULE_VERSION(FILEVER_STR);
MODULE_AUTHOR("Veeam Software Group GmbH");
MODULE_LICENSE("GPL");
