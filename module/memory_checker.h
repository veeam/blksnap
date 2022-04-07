/* SPDX-License-Identifier: GPL-2.0 */
#pragma once
#ifdef CONFIG_BLK_SNAP_DEBUG_MEMORY_LEAK

enum memory_object_type {
	/*alloc_page*/
	memory_object_page,
	/*kzalloc*/
	memory_object_cbt_map,
	memory_object_chunk,
	memory_object_blk_snap_snapshot_event,
	memory_object_diff_area,
	memory_object_big_buffer,
	memory_object_diff_io,
	memory_object_diff_storage,
	memory_object_storage_bdev,
	memory_object_storage_block,
	memory_object_diff_region,
	memory_object_diff_buffer,
	memory_object_event,
	memory_object_snapimage,
	memory_object_snapshot,
	memory_object_tracker,
	memory_object_tracked_device,
	/*kcalloc*/
	memory_object_blk_snap_cbt_info,
	memory_object_blk_snap_block_range,
	memory_object_blk_snap_dev_t,
	memory_object_tracker_array,
	memory_object_snapimage_array,
	memory_object_superblock_array,
	memory_object_blk_snap_image_info,
	/*end*/
	memory_object_count
};

void memory_object_inc(enum memory_object_type type);
void memory_object_dec(enum memory_object_type type);
int  memory_object_print(void);
void memory_object_max_print(void);
#endif
