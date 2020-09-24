/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "sector.h"

int blk_dev_open(dev_t dev_id, struct block_device **p_blk_dev);
void blk_dev_close(struct block_device *blk_dev);

static inline sector_t blk_dev_get_capacity(struct block_device *blk_dev)
{
	return blk_dev->bd_part->nr_sects;
};
