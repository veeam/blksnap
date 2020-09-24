/* SPDX-License-Identifier: GPL-2.0 */
#pragma once

#include "blk_descr_mem.h"

struct snapstore_mem {
	struct list_head blocks;
	struct mutex blocks_lock;

	size_t blocks_limit;
	size_t blocks_allocated;

	struct blk_descr_pool pool;
};

struct snapstore_mem *snapstore_mem_create(size_t available_blocks);

void snapstore_mem_destroy(struct snapstore_mem *mem);

void *snapstore_mem_get_block(struct snapstore_mem *mem);
