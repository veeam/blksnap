
// SPDX-License-Identifier: GPL-2.0
#include "common.h"
#include <linux/module.h>

static int g_param_snapstore_block_size_pow = 14;
static int g_param_change_tracking_block_size_pow = 18;


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

void params_check(void)
{
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
}

module_param_named(snapstore_block_size_pow, g_param_snapstore_block_size_pow, int, 0644);
MODULE_PARM_DESC(snapstore_block_size_pow,
		 "Snapstore block size binary pow. 20 for 1MiB block size");

module_param_named(change_tracking_block_size_pow, g_param_change_tracking_block_size_pow, int,
		   0644);
MODULE_PARM_DESC(change_tracking_block_size_pow,
		 "Change-tracking block size binary pow. 18 for 256 KiB block size");
