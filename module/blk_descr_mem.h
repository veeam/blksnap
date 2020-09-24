/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "blk_descr_pool.h"

struct blk_descr_mem {
	void *buff; //pointer to snapstore block in memory
};

void blk_descr_mem_pool_init(struct blk_descr_pool *pool, size_t available_blocks);
void blk_descr_mem_pool_done(struct blk_descr_pool *pool);

int blk_descr_mem_pool_add(struct blk_descr_pool *pool, void *buffer);
union blk_descr_unify blk_descr_mem_pool_take(struct blk_descr_pool *pool);
