#pragma once
#include "blk-snap-ctl.h"

int snapimage_init(void);
int snapimage_done(void);
int snapimage_create_for(dev_t *p_dev, int count);
int snapimage_stop(dev_t original_dev);
int snapimage_destroy(dev_t original_dev);

int snapimage_collect_images(int count, struct image_info_s *p_user_image_info, int *p_real_count);

int snapimage_mark_dirty_blocks(dev_t image_dev_id, struct block_range_s *block_ranges,
				unsigned int count);
