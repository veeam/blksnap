/* SPDX-License-Identifier: GPL-2.0 */
#pragma once

#include "blk_snap.h"

struct snapimage {
	struct list_head link;
	struct kref kref;
	dev_t image_dev;
	sector_t capacity;


	struct diff_area *diff_area;
	struct cbt_map *cbt_map;

	//struct mutex open_locker;
	//struct block_device *open_bdev;
	//size_t open_cnt;
};

int snapimage_init(void);
void snapimage_done(void);

void snapimage_free(struct kref *kref);
struct snapimage *snapimage_create(struct diff_area *diff_area, struct cbt_map *cbt_map);
static inline void snapimage_put(struct snapimage *snapimage)
{
	snapimage_free(snapimage->kref);
}
/*
<!-- I'm staying here. --!>
*/

//void snapimage_stop(dev_t orig_dev_id);


//int snapimage_collect_images(int count, struct image_info_s *p_user_image_info, int *p_real_count);

//int snapimage_mark_dirty_blocks(dev_t image_dev_id, struct block_range_s *block_ranges,
//				unsigned int count);
