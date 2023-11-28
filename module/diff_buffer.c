// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Veeam Software Group GmbH */
#define pr_fmt(fmt) KBUILD_MODNAME "-diff-buffer: " fmt

#include "diff_buffer.h"
#include "diff_area.h"
#include "params.h"

static void diff_buffer_free(struct diff_buffer *diff_buffer)
{
	size_t inx = 0;

	if (unlikely(!diff_buffer))
		return;

	for (inx = 0; inx < diff_buffer->nr_pages; inx++)
		__free_page(diff_buffer->bvec[inx].bv_page);

	kfree(diff_buffer);
}

static struct diff_buffer *diff_buffer_new(size_t nr_pages, size_t size,
					   gfp_t gfp_mask)
{
	struct diff_buffer *diff_buffer;
	size_t inx = 0;

	if (unlikely(nr_pages <= 0))
		return NULL;

	diff_buffer = kzalloc(sizeof(struct diff_buffer) +
			      nr_pages * sizeof(struct bio_vec),
			      gfp_mask);
	if (!diff_buffer)
		return NULL;

	INIT_LIST_HEAD(&diff_buffer->link);
	diff_buffer->size = size;
	diff_buffer->nr_pages = nr_pages;

	for (inx = 0; inx < nr_pages; inx++) {
		struct page *page = alloc_page(gfp_mask);

		if (!page)
			goto fail;
		bvec_set_page(&diff_buffer->bvec[inx], page, PAGE_SIZE, 0);
	}
	return diff_buffer;
fail:
	diff_buffer_free(diff_buffer);
	return NULL;
}

struct diff_buffer *diff_buffer_take(struct diff_area *diff_area)
{
	struct diff_buffer *diff_buffer = NULL;
	sector_t chunk_sectors;
	size_t page_count;

	spin_lock(&diff_area->free_diff_buffers_lock);
	diff_buffer = list_first_entry_or_null(&diff_area->free_diff_buffers,
					       struct diff_buffer, link);
	if (diff_buffer) {
		list_del(&diff_buffer->link);
		atomic_dec(&diff_area->free_diff_buffers_count);
	}
	spin_unlock(&diff_area->free_diff_buffers_lock);

	/* Return free buffer if it was found in a pool */
	if (diff_buffer)
		return diff_buffer;

	/* Allocate new buffer */
	chunk_sectors = diff_area_chunk_sectors(diff_area);
	page_count = round_up(chunk_sectors, PAGE_SECTORS) / PAGE_SECTORS;
	diff_buffer = diff_buffer_new(page_count, chunk_sectors << SECTOR_SHIFT,
				      GFP_NOIO);
	if (unlikely(!diff_buffer))
		return ERR_PTR(-ENOMEM);
	return diff_buffer;
}

void diff_buffer_release(struct diff_area *diff_area,
			 struct diff_buffer *diff_buffer)
{
	if (atomic_read(&diff_area->free_diff_buffers_count) >
	    get_free_diff_buffer_pool_size()) {
		diff_buffer_free(diff_buffer);
		return;
	}
	spin_lock(&diff_area->free_diff_buffers_lock);
	list_add_tail(&diff_buffer->link, &diff_area->free_diff_buffers);
	atomic_inc(&diff_area->free_diff_buffers_count);
	spin_unlock(&diff_area->free_diff_buffers_lock);
}

void diff_buffer_cleanup(struct diff_area *diff_area)
{
	struct diff_buffer *diff_buffer = NULL;

	do {
		spin_lock(&diff_area->free_diff_buffers_lock);
		diff_buffer =
			list_first_entry_or_null(&diff_area->free_diff_buffers,
						 struct diff_buffer, link);
		if (diff_buffer) {
			list_del(&diff_buffer->link);
			atomic_dec(&diff_area->free_diff_buffers_count);
		}
		spin_unlock(&diff_area->free_diff_buffers_lock);

		if (diff_buffer)
			diff_buffer_free(diff_buffer);
	} while (diff_buffer);
}
