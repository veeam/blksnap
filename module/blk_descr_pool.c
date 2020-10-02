// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-blk_descr"
#include "common.h"
#include "blk_descr_pool.h"
#include "params.h"

struct pool_el {
	struct list_head link;

	size_t used_cnt; // used blocks
	size_t capacity; // blocks array capacity

	u8 descr_array[0];
};

static void *kmalloc_huge(size_t max_size, size_t min_size, gfp_t flags, size_t *p_allocated_size)
{
	void *ptr = NULL;

	do {
		ptr = kmalloc(max_size, flags | __GFP_NOWARN | __GFP_RETRY_MAYFAIL);

		if (ptr != NULL) {
			*p_allocated_size = max_size;
			return ptr;
		}
		pr_err("Failed to allocate buffer size=%lu\n", max_size);
		max_size = max_size >> 1;
	} while (max_size >= min_size);

	pr_err("Failed to allocate buffer.");
	return NULL;
}

static struct pool_el *pool_el_alloc(size_t blk_descr_size)
{
	size_t el_size;
	struct pool_el *el;

	el = kmalloc_huge(8 * PAGE_SIZE, PAGE_SIZE, GFP_NOIO, &el_size);
	if (el == NULL)
		return NULL;

	el->capacity = (el_size - sizeof(struct pool_el)) / blk_descr_size;
	el->used_cnt = 0;

	INIT_LIST_HEAD(&el->link);

	return el;
}

static void _pool_el_free(struct pool_el *el)
{
	if (el != NULL)
		kfree(el);
}

void blk_descr_pool_init(struct blk_descr_pool *pool, size_t available_blocks)
{
	mutex_init(&pool->lock);

	INIT_LIST_HEAD(&pool->head);

	pool->blocks_cnt = 0;

	pool->total_cnt = available_blocks;
	pool->take_cnt = 0;
}

void blk_descr_pool_done(struct blk_descr_pool *pool,
			 void (*blocks_cleanup_cb)(void *descr_array, size_t count))
{
	mutex_lock(&pool->lock);
	while (!list_empty(&pool->head)) {
		struct pool_el *el;

		el = list_entry(pool->head.next, struct pool_el, link);
		if (el == NULL)
			break;

		list_del(&el->link);
		--pool->blocks_cnt;

		pool->total_cnt -= el->used_cnt;

		blocks_cleanup_cb(el->descr_array, el->used_cnt);

		_pool_el_free(el);
	}
	mutex_unlock(&pool->lock);
}

union blk_descr_unify blk_descr_pool_alloc(
	struct blk_descr_pool *pool, size_t blk_descr_size,
	union blk_descr_unify (*block_alloc_cb)(void *descr_array, size_t index, void *arg),
	void *arg)
{
	union blk_descr_unify blk_descr = { NULL };

	mutex_lock(&pool->lock);
	do {
		struct pool_el *el = NULL;

		if (!list_empty(&pool->head)) {
			el = list_entry(pool->head.prev, struct pool_el, link);
			if (el->used_cnt == el->capacity)
				el = NULL;
		}

		if (el == NULL) {
			el = pool_el_alloc(blk_descr_size);
			if (el == NULL)
				break;

			list_add_tail(&el->link, &pool->head);

			++pool->blocks_cnt;
		}

		blk_descr = block_alloc_cb(el->descr_array, el->used_cnt, arg);

		++el->used_cnt;
		++pool->total_cnt;

	} while (false);
	mutex_unlock(&pool->lock);

	return blk_descr;
}

static union blk_descr_unify __blk_descr_pool_at(struct blk_descr_pool *pool, size_t blk_descr_size,
						 size_t index)
{
	union blk_descr_unify bkl_descr = { NULL };
	size_t curr_inx = 0;
	struct pool_el *el;
	struct list_head *_list_head;

	if (list_empty(&(pool)->head))
		return bkl_descr;

	list_for_each(_list_head, &(pool)->head) {
		el = list_entry(_list_head, struct pool_el, link);

		if ((index >= curr_inx) && (index < (curr_inx + el->used_cnt))) {
			bkl_descr.ptr = el->descr_array + (index - curr_inx) * blk_descr_size;
			break;
		}
		curr_inx += el->used_cnt;
	}

	return bkl_descr;
}

union blk_descr_unify blk_descr_pool_take(struct blk_descr_pool *pool, size_t blk_descr_size)
{
	union blk_descr_unify result = { NULL };

	mutex_lock(&pool->lock);
	do {
		if (pool->take_cnt >= pool->total_cnt) {
			pr_err("Unable to get block descriptor: ");
			pr_err("not enough descriptors, already took %ld, total %ld\n",
			       pool->take_cnt, pool->total_cnt);
			break;
		}

		result = __blk_descr_pool_at(pool, blk_descr_size, pool->take_cnt);
		if (result.ptr == NULL) {
			pr_err("Unable to get block descriptor: ");
			pr_err("not enough descriptors, already took %ld, total %ld\n",
			       pool->take_cnt, pool->total_cnt);
			break;
		}

		++pool->take_cnt;
	} while (false);
	mutex_unlock(&pool->lock);
	return result;
}

bool blk_descr_pool_check_halffill(struct blk_descr_pool *pool, sector_t empty_limit,
				   sector_t *fill_status)
{
	size_t empty_blocks = (pool->total_cnt - pool->take_cnt);

	*fill_status = (sector_t)(pool->take_cnt) << snapstore_block_shift();

	return (empty_blocks < (size_t)(empty_limit >> snapstore_block_shift()));
}
