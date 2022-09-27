// SPDX-License-Identifier: GPL-2.0
#ifdef CONFIG_BLK_SNAP_DEBUG_MEMORY_LEAK
#define pr_fmt(fmt) KBUILD_MODNAME "-memory_checker: " fmt
#include <linux/atomic.h>
#include <linux/module.h>
#include "memory_checker.h"
#ifdef STANDALONE_BDEVFILTER
#include "log.h"
#endif

char *memory_object_names[] = {
	/*alloc_page*/
	"page",
	/*kzalloc*/
	"cbt_map",
	"cbt_buffer",
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
	"log_filepath",
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

int memory_object_print(bool is_error)
{
	int inx;
	int not_free = 0;

	pr_debug("Objects in memory:\n");
	for (inx = 0; inx < memory_object_count; inx++) {
		int count = atomic_read(&memory_counter[inx]);

		if (count) {
			not_free += count;
			if (is_error) {
				pr_err("%s: %d\n", memory_object_names[inx],
					count);
			} else {
				pr_debug("%s: %d\n", memory_object_names[inx],
					count);
			}
		}
	}
	if (not_free)
		if (is_error)
			pr_err("%d not released objects found\n", not_free);
		else
			pr_debug("Found %d allocated objects\n", not_free);
	else
		pr_debug("All objects have been released\n");
	return not_free;
}

void memory_object_max_print(void)
{
	int inx;

	pr_debug("Maximim objects in memory:\n");
	for (inx = 0; inx < memory_object_count; inx++) {
		int count = atomic_read(&memory_counter_max[inx]);

		if (count)
			pr_debug("%s: %d\n", memory_object_names[inx], count);
	}
	pr_debug(".\n");
}
#endif
