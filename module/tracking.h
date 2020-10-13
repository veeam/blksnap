/* SPDX-License-Identifier: GPL-2.0 */
#pragma once
#include "blk-snap-ctl.h"
#include <linux/bio.h>

int  tracking_init(void);
void tracking_done(void);

int tracking_add(dev_t dev_id, unsigned long long snapshot_id);
int tracking_remove(dev_t dev_id);
int tracking_collect(int max_count, struct cbt_info_s *p_cbt_info, int *p_count);
int tracking_read_cbt_bitmap(dev_t dev_id, unsigned int offset, size_t length,
			     void __user *user_buff);
