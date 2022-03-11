// SPDX-License-Identifier: GPL-2.0
#ifdef CONFIG_BLK_SNAP_DEBUG_MEMORY_LEAK
#define pr_fmt(fmt) KBUILD_MODNAME "-memory_checker: " fmt
#include <linux/atomic.h>
#include <linux/module.h>
#include "memory_checker.h"

char *memory_object_names[] = {
	/*alloc_page*/
	"page",
	/*kzalloc*/
	"cbt_map",
	"chunk",
	"blk_snap_snaphot_event",
	"diff_area",
	"big_buffer",
	"diff_io",
	"diff_storage",
	"storage_bdev",
	"storage_block",
	"diff_region",
	"diff_buffer",
	"event",
	"snapimage",
	"snapshot",
	"tracker",
	"tracked_device",
	/*kcalloc*/
	"blk_snap_cbt_info",
	"blk_snap_block_range",
	"blk_snap_dev_t",
	"tracker_array",
	"snapimage_array",
	"superblock_array",
	"blk_snap_image_info",
	/*end*/
};

static_assert(
	sizeof(memory_object_names) == (memory_object_count * sizeof(char *)),
	"The size of enum memory_object_type is not equal to size of memory_object_names array.");

static atomic_t memory_counter[memory_object_count];
static atomic_t memory_counter_max[memory_object_count];

void memory_object_inc(enum memory_object_type type)
{
	int value;

	if (unlikely(type >= memory_object_count))
		return;

	value = atomic_inc_return(&memory_counter[type]);
	if (value > atomic_read(&memory_counter_max[type]))
		atomic_inc(&memory_counter_max[type]);
}

void memory_object_dec(enum memory_object_type type)
{
	if (unlikely(type >= memory_object_count))
		return;

	atomic_dec(&memory_counter[type]);
}

void memory_object_print(void)
{
	int cnt;

	pr_info("Statistics for objects in memory:\n");
	for (cnt = 0; cnt < memory_object_count; cnt++)
		pr_info("%s: %d\n", memory_object_names[cnt],
			 atomic_read(&memory_counter[cnt]));

	pr_info("Maximim for objects in memory:\n");
	for (cnt = 0; cnt < memory_object_count; cnt++)
		pr_info("%s: %d\n", memory_object_names[cnt],
			 atomic_read(&memory_counter_max[cnt]));
}

#endif
