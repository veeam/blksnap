// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-cache"
#include <linux/blk_types.h>
#include <linux/blk.h>
#include <linux/blkdev.h>
#include "common.h"
#include "cache.h"
#include "diff_area.h"




//void diff_buffer_free(struct kref *kref)
static inline
void diff_buffer_free(struct diff_buffer *diff_buffer)
{
	struct page_list *curr_page = diff_buffer.pages;
	
	while(curr_page) {
		if (curr_page->page)
			__free_page(curr_page->page);//free_page((unsigned long)page_address(curr_page->page));

		curr_page = curr_page->next;
	}
	kfree(diff_buffer);
}

static inline
struct diff_buffer *diff_buffer_new(size_t page_count, size_t buffer_size, gfp_t gfp_mask)
{
	int ret = 0;
	struct diff_buffer *buf;
	size_t inx;
	struct page_list *prev_page = NULL;

	buf = kzalloc(sizeof(struct diff_buffer) + page_count * sizeof(struct page_list), gfp_mask);
	if (!buf)
		return NULL;

	buf->size = buffer_size;
	
	for (inx = 0; inx < page_count; inx++) {
		struct page *page;

		page = alloc_page(gfp_mask);
		if (!page) {
			ret = -ENOMEM;
			break;
		}
		buf->pages[inx].page = page;

		if (inx)
			prev_page->next = buf->pages + inx;
		prev_page = buf->pages + inx;
	}
	pr_debug("allocate %d pages\n", inx);
}

struct chunk *chunk_alloc(struct diff_area *diff_area, unsigned long number)
{
	int ret = 0;
	struct chunk *chunk;

	pr_debug("allocate chunk sz=%ld\n", sizeof(struct chunk));
	chunk = kzalloc(sizeof(struct chunk), GFP_KERNEL);
	if (!chunk)
		return NULL;

	kref_init(&chunk->refcount);
	INIT_LIST_HEAD(&chunk->link);
	init_rwsem(&chunk->lock);
	chunk->diff_area = diff_area;
	chunk->number = number;

	return chunk;
}

void chunk_free(struct chunk *chunk)
{
	if (!chunk)
		return;

	if (chunk->diff_buffer) 
		diff_buffer_put(chunk->diff_buffer);

	if (chunk->diff_store)
		diff_store_free(chunk->diff_store);

	kfree(chunk);
}
/*
void chunk_free(struct kref *kref)
{
	__chunk_free(container_of(kref, struct chunk, refcount));
}
*/

/**
 * chunk_allocate_buffer - allocate diff buffer.
 * 
 * Don't forget to lock the chunk for writing before allocating the buffer.
 */
int chunk_allocate_buffer(struct chunk *chunk, gfp_t gfp_mask)
{
	struct diff_buffer *buf;
	size_t page_count;
	size_t buffer_size;

	page_count = round_up(chunk->sector_count, SECTOR_IN_PAGE);
	buffer_size = chunk->sector_count << SECTOR_SHIFT;

	buf = diff_buffer_new(page_count, buffer_size, gfp_mask);
	if (!buf) {
		pr_err("Failed allocate memory buffer for chunk");
		return -ENOMEM;
	}
	chunk->diff_buffer = buf;	
}

/**
 * chunk_free_buffer - free diff buffer.
 * 
 * Don't forget to lock the chunk for writing before freeing the buffer.
 */
void chunk_free_buffer(struct chunk *chunk)
{
	diff_buffer_free(chunk->diff_buffer);
	chunk->diff_buffer = NULL;
	chunk_state_unset(chunk, CHUNK_ST_BUFFER_READY);
}

LIST_HEAD(shared_chunk_cache);
DEFINE_SPINLOCK(shared_chunk_cache_lock);
sector_t shared_chunk_cache_count;

/*
void chunk_cache_store(struct chunk *chunk)
{


	spin_lock(&shared_chunk_cache_lock);

	list_add_tail(&chunk->link, shared_chunk_cache);
	shared_chunk_cache_count++;	
	chunk = NULL;

	if (shared_chunk_cache_length > CONFIG_BLK_SNAP_MAXIMUM_CHUNK_IN_CACHE) {
		chunk = list_first_entry(&shared_chunk_cache, struct chunk, link);
		list_del(&chunk->link);
		shared_chunk_cache_count--;
	}

	spin_unlock(&shared_chunk_cache_lock);

	if (chunk) {
		down_write(&chunk->buffer_lock);
		chunk_free_buffer(chunk);
		up_write(&chunk->buffer_lock);		
	}
}


void chunk_cache_flush(struct chunk *chunk)
{
	struct chunk *chunk = NULL;

	while (1) {
		spin_lock(&shared_chunk_cache_lock);
		chunk = list_first_entry_or_null(&shared_chunk_cache, struct chunk, link);
		if (!chunk) {
			spin_unlock(&shared_chunk_cache_lock);
			break;
		}
		list_del(chunk->link);
		shared_chunk_cache_count--;
		spin_unlock(&shared_chunk_cache_lock);

		__flush_chunk(chunk);
	}
}
*/
/**
 * chunk_async_store_diff - Starts asynchronous storing of a chunk to
 * the difference storage.
 */
int chunk_async_store_diff(struct chunk *chunk, io_notify_fn *fn)
{
	unsigned long sync_error_bits;
	struct dm_io_region region;
	struct dm_io_request reguest;

	dm_io_region region = {
		.bdev = chunk->diff_store->bdev,
		.sector = chunk->diff_store->sector,
		.count = chunk->sector_count,
	};
	dm_io_request reguest = {
		.bi_op = REQ_OP_WRITE,
		.bi_op_flags = 0,
		.mem.type = DM_IO_PAGE_LIST,
		.mem.offset = 0,
		.mem.ptr.pl = chunk_first_page(chunk),
		.notify.fn = fn;
		.notify.context = chunk,
		.client = chunk->diff_area->io_client,
	};

	return dm_io(&reguest, 1, &region, &sync_error_bits);
}

/**
 * chunk_asunc_load_orig - Starts asynchronous loading of a chunk from 
 * the origian block device.
 */
int chunk_asunc_load_orig(struct chunk *chunk, io_notify_fn *fn)
{
	unsigned long sync_error_bits;
	struct dm_io_region region = {
		.bdev = chunk->diff_area->orig_bdev,
		.sector = (sector_t)chunk->number * chunk_sectors(chunk->diff_area),
		.count = chunk->sector_count,
	};
	struct dm_io_request reguest = {
		.bi_op = REQ_OP_READ,
		.bi_op_flags = 0,
		.mem.type = DM_IO_PAGE_LIST,
		.mem.offset = 0,
		.mem.ptr.pl = chunk_first_page(chunk),
		.notify.fn = fn,
		.notify.context = chunk,
		.client = chunk->diff_area->io_client,
	};

	return dm_io(&reguest, 1, &region, &sync_error_bits);
}

/**
 * chunk_load_orig - Performs synchronous loading of a chunk from the original
 * block device.
 */
int chunk_load_orig(struct chunk *chunk)
{
	unsigned long sync_error_bits = 0;
	struct dm_io_region region = {
		.bdev = chunk->diff_area->orig_bdev,
		.sector = (sector_t)chunk->number * chunk_sectors(chunk->diff_area),
		.count = chunk->sector_count,
	};
	struct dm_io_request reguest = {
		.bi_op = REQ_OP_READ,
		.bi_op_flags = 0,
		.mem.type = DM_IO_PAGE_LIST,
		.mem.offset = 0,
		.mem.ptr.pl = chunk_first_page(chunk),
		.notify.fn = NULL,
		.notify.context = NULL,
		.client = chunk->diff_area->io_client,
	};

	return dm_io(&reguest, 1, &region, &sync_error_bits);
}

/**
 * chunk_load_diff - Performs synchronous loading of a chunk from the
 * difference storage.
 */
int chunc_load_diff(struct chunk *chunk)
{
	unsigned long sync_error_bits = 0;
	struct dm_io_region region = {
		.bdev = chunk->diff_store->bdev,
		.sector = chunk->diff_store->sector,
		.count = chunk->diff_store->count,
	};
	struct dm_io_request reguest = {
		.bi_op = REQ_OP_READ,
		.bi_op_flags = 0,
		.mem.type = DM_IO_PAGE_LIST,
		.mem.offset = 0,
		.mem.ptr.pl = chunk_first_page(chunk),
		.notify.fn = NULL,
		.notify.context = NULL,
		.client = chunk->diff_area->io_client,
	};

	return dm_io(&reguest, 1, &region, &sync_error_bits);
}
/**
 * chunk_load_diff - Performs synchronous storing of a chunk to the
 * difference storage.
 */
/*
int chunc_store_diff(struct chunk *chunk, struct iov_iter *iter)
{
	unsigned long sync_error_bits = 0;
	struct dm_io_region region = {
		.bdev = chunk->diff_store->bdev,
		.sector = chunk->diff_store->sector,
		.count = chunk->diff_store->count,
	};
	struct dm_io_request reguest = {
		.bi_op = REQ_OP_WRITE,
		.bi_op_flags = 0,
		.mem.type = DM_IO_PAGE_LIST,
		.mem.offset = 0,
		.mem.ptr.pl = chunk_first_page(chunk),
		.notify.fn = NULL,
		.notify.context = NULL,
		.client = chunk->diff_area->io_client,
	};

	return dm_io(&reguest, 1, &region, &sync_error_bits);
}
*/
/*
int chunc_iter_store_diff(struct chunk *chunk, struct iov_iter *iter)
{
	chunk->diff_buffer
	//chunk_state_set(chunk, CHUNK_ST_DIRTY);
	sz = copy_page_from_iter();

	!!!! Что делать с флагом CHUNK_ST_DIRTY
	!!!! Нужно поместит кусок в очередь для сохранения в diff storage

	

}

*/
