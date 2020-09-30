/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

struct blk_descr_mem;
struct blk_descr_file;
struct blk_descr_multidev;

union blk_descr_unify {
	void *ptr;
	struct blk_descr_mem *mem;
	struct blk_descr_file *file;
	struct blk_descr_multidev *multidev;
};

struct blk_descr_pool {
	struct list_head head;
	struct mutex lock;

	size_t blocks_cnt; // count of struct pool_el

	size_t total_cnt;  // total count of block descriptors
	size_t take_cnt;   // take count of block descriptors
};

void blk_descr_pool_init(struct blk_descr_pool *pool, size_t available_blocks);

void blk_descr_pool_done(struct blk_descr_pool *pool,
			 void (*blocks_cleanup_cb)(void *descr_array, size_t count));

union blk_descr_unify blk_descr_pool_alloc(
	struct blk_descr_pool *pool, size_t blk_descr_size,
	union blk_descr_unify (*block_alloc_cb)(void *descr_array, size_t index, void *arg),
	void *arg);

union blk_descr_unify blk_descr_pool_take(struct blk_descr_pool *pool, size_t blk_descr_size);

bool blk_descr_pool_check_halffill(struct blk_descr_pool *pool, sector_t empty_limit,
				   sector_t *fill_status);
