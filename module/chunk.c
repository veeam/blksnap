// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME "-chunk: " fmt
#include <linux/slab.h>
#include <linux/dm-io.h>

#include "chunk.h"
#include "diff_area.h"
#include "diff_storage.h"

static
void diff_buffer_free(struct diff_buffer *diff_buffer)
{
	struct page_list *curr_page;

	if (unlikely(!diff_buffer))
		return;

	curr_page = diff_buffer->pages;
	while(curr_page) {
		if (curr_page->page)
			__free_page(curr_page->page);

		curr_page = curr_page->next;
	}
	kfree(diff_buffer);
}

static
struct diff_buffer *diff_buffer_new(size_t page_count, size_t buffer_size,
                                    gfp_t gfp_mask)
{
	struct diff_buffer *diff_buffer;
	size_t inx;
	struct page_list *prev_page;
	struct page_list *curr_page;
	struct page *page;

	if (unlikely(page_count <= 0))
		return NULL;

	diff_buffer = kzalloc(sizeof(struct diff_buffer) + page_count * sizeof(struct page_list),
	                      gfp_mask);
	if (!diff_buffer)
		return NULL;

	diff_buffer->size = buffer_size;

	/* Allocate first page */
	page = alloc_page(gfp_mask);
	if (!page)
		goto fail;

	diff_buffer->pages[0].page = page;
	prev_page = diff_buffer->pages;

	/* Allocate all other pages and make list link */
	for (inx = 1; inx < page_count; inx++) {
		page = alloc_page(gfp_mask);
		if (!page)
			goto fail;

		curr_page = prev_page + 1;
		curr_page->page = page;

		prev_page->next = curr_page;
		prev_page = curr_page;
	}

	return diff_buffer;
fail:
	diff_buffer_free(diff_buffer);
	return NULL;
}



struct chunk *chunk_alloc(struct diff_area *diff_area, unsigned long number)
{
	struct chunk *chunk;

	chunk = kzalloc(sizeof(struct chunk), GFP_KERNEL);
	if (!chunk)
		return NULL;

	INIT_LIST_HEAD(&chunk->storage_link);
	INIT_LIST_HEAD(&chunk->cache_link);
	init_rwsem(&chunk->lock);
	chunk->diff_area = diff_area;
	chunk->number = number;

	return chunk;
}

void chunk_free(struct chunk *chunk)
{
	if (unlikely(!chunk))
		return;

	diff_buffer_free(chunk->diff_buffer);
	kfree(chunk->diff_store);
	kfree(chunk);
}

/**
 * chunk_allocate_buffer() - Allocate diff buffer.
 * @chunk:
 * 	?
 * @gfp_mask:
 * 	?
 *
 * Don't forget to lock the chunk for writing before allocating the buffer.
 */
int chunk_allocate_buffer(struct chunk *chunk, gfp_t gfp_mask)
{
	struct diff_buffer *buf;
	size_t page_count;
	size_t buffer_size;

	page_count = round_up(chunk->sector_count, SECTOR_IN_PAGE) / SECTOR_IN_PAGE;
	buffer_size = chunk->sector_count << SECTOR_SHIFT;

	buf = diff_buffer_new(page_count, buffer_size, gfp_mask);
	if (!buf) {
		pr_err("Failed allocate memory buffer for chunk");
		return -ENOMEM;
	}
	chunk->diff_buffer = buf;

	return 0;
}

/**
 * chunk_free_buffer() - Free diff buffer.
 * @chunk:
 * 	?
 *
 * Don't forget to lock the chunk for writing before freeing the buffer.
 */
void chunk_free_buffer(struct chunk *chunk)
{
	diff_buffer_free(chunk->diff_buffer);
	chunk->diff_buffer = NULL;
	chunk_state_unset(chunk, CHUNK_ST_BUFFER_READY);
}

static inline
struct page_list *chunk_first_page(struct chunk *chunk)
{
	return chunk->diff_buffer->pages;
};

/**
 * chunk_async_store_diff() - Starts asynchronous storing of a chunk to the
 *	difference storage.
 *
 */
int chunk_async_store_diff(struct chunk *chunk, io_notify_fn fn)
{
	unsigned long sync_error_bits;
	struct dm_io_region region = {
		.bdev = chunk->diff_store->bdev,
		.sector = chunk->diff_store->sector,
		.count = chunk->sector_count,
	};
	struct dm_io_request reguest = {
		.bi_op = REQ_OP_WRITE,
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
 * chunk_asunc_load_orig() - Starts asynchronous loading of a chunk from
 * 	the origian block device.
 */
int chunk_asunc_load_orig(struct chunk *chunk, io_notify_fn fn)
{
	unsigned long sync_error_bits;
	struct dm_io_region region = {
		.bdev = chunk->diff_area->orig_bdev,
		.sector = (sector_t)(chunk->number) * diff_area_chunk_sectors(chunk->diff_area),
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
 * chunk_load_orig() - Performs synchronous loading of a chunk from the
 * 	original block device.
 */
int chunk_load_orig(struct chunk *chunk)
{
	unsigned long sync_error_bits = 0;
	struct dm_io_region region = {
		.bdev = chunk->diff_area->orig_bdev,
		.sector = (sector_t)chunk->number * diff_area_chunk_sectors(chunk->diff_area),
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
 * chunk_load_diff() - Performs synchronous loading of a chunk from the
 * 	difference storage.
 */
int chunk_load_diff(struct chunk *chunk)
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
