// SPDX-License-Identifier: GPL-2.0
#include "common.h"
#include "version.h"
#include "blk-snap-ctl.h"
#include "params.h"
#include "ctrl_fops.h"
#include "ctrl_pipe.h"
#include "ctrl_sysfs.h"
#include "snapimage.h"
#include "snapstore.h"
#include "snapstore_device.h"
#include "snapshot.h"
#include "tracker.h"
#include "tracking.h"
#include <linux/module.h>

static int __init blk_snap_init(void)
{
	int result = SUCCESS;

	pr_info("Loading\n");

	params_check();

	result = ctrl_init();
	if (result != SUCCESS)
		return result;

	result = blk_bioset_create();
	if (result != SUCCESS)
		return result;

	result = blk_redirect_bioset_create();
	if (result != SUCCESS)
		return result;

	result = blk_deferred_bioset_create();
	if (result != SUCCESS)
		return result;

	result = snapimage_init();
	if (result != SUCCESS)
		return result;

	result = ctrl_sysfs_init();
	if (result != SUCCESS)
		return result;

	result = tracking_init();
	if (result != SUCCESS)
		return result;

	return result;
}

static void __exit blk_snap_exit(void)
{
	pr_info("Unloading module\n");

	ctrl_sysfs_done();

	snapshot_done();

	snapstore_device_done();
	snapstore_done();

	tracker_done();
	tracking_done();

	snapimage_done();

	blk_deferred_bioset_free();
	blk_deferred_done();

	blk_redirect_bioset_free();

	blk_bioset_free();

	ctrl_done();
}

module_init(blk_snap_init);
module_exit(blk_snap_exit);

MODULE_DESCRIPTION("Block Layer Snapshot Kernel Module");
MODULE_VERSION(FILEVER_STR);
MODULE_AUTHOR("Veeam Software Group GmbH");
MODULE_LICENSE("GPL");
