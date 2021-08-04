// SPDX-License-Identifier: GPL-2.0
#include "common.h"
#include "params.h"
#include <linux/module.h>

int snapstore_block_size_pow = 14;
int change_tracking_block_size_pow = 18;

int get_snapstore_block_size_pow(void)
{
	return snapstore_block_size_pow;
}

int inc_snapstore_block_size_pow(void)
{
	if (snapstore_block_size_pow > 30)
		return -EFAULT;

	++snapstore_block_size_pow;
	return 0;
}

int get_change_tracking_block_size_pow(void)
{
	return change_tracking_block_size_pow;
}

void params_check(void)
{
	pr_info("snapstore_block_size_pow: %d\n", snapstore_block_size_pow);
	pr_info("change_tracking_block_size_pow: %d\n", change_tracking_block_size_pow);

	if (snapstore_block_size_pow > 23) {
		snapstore_block_size_pow = 23;
		pr_info("Limited snapstore_block_size_pow: %d\n", snapstore_block_size_pow);
	} else if (snapstore_block_size_pow < 12) {
		snapstore_block_size_pow = 12;
		pr_info("Limited snapstore_block_size_pow: %d\n", snapstore_block_size_pow);
	}

	if (change_tracking_block_size_pow > 23) {
		change_tracking_block_size_pow = 23;
		pr_info("Limited change_tracking_block_size_pow: %d\n",
			change_tracking_block_size_pow);
	} else if (change_tracking_block_size_pow < 12) {
		change_tracking_block_size_pow = 12;
		pr_info("Limited change_tracking_block_size_pow: %d\n",
			change_tracking_block_size_pow);
	}
}

module_param_named(snapstore_block_size_pow, snapstore_block_size_pow, int, 0644);
MODULE_PARM_DESC(snapstore_block_size_pow,
		 "Snapstore block size binary pow. 20 for 1MiB block size");

module_param_named(change_tracking_block_size_pow, change_tracking_block_size_pow, int, 0644);
MODULE_PARM_DESC(change_tracking_block_size_pow,
		 "Change-tracking block size binary pow. 18 for 256 KiB block size");
