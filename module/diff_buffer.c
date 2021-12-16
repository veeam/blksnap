// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME "-diff-buffer: " fmt

#include "params.h"
#include "diff_buffer.h"
#include "diff_area.h"

#ifdef CONFIG_DEBUGLOG
#undef pr_debug
#define pr_debug(fmt, ...) \
	printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
#endif

#ifdef CONFIG_DEBUG_DIFF_BUFFER
static atomic_t diff_buffer_allocated_counter;

int diff_buffer_allocated_counter_get(void )
{
	return atomic_read(&diff_buffer_allocated_counter);
}
#endif

void diff_buffer_free(struct diff_buffer *diff_buffer)
{
	size_t inx = 0;
	struct page *page;

	if (unlikely(!diff_buffer))
		return;

	for (inx = 0; inx < diff_buffer->page_count; inx++) {
		page = diff_buffer->pages[inx];
		if (page)
			__free_page(page);
	}

	kfree(diff_buffer);
#ifdef CONFIG_DEBUG_DIFF_BUFFER
	pr_debug("Free buffer #%d \n",
		atomic_read(&diff_buffer_allocated_counter));
	atomic_dec(&diff_buffer_allocated_counter);
#endif
}

struct diff_buffer *diff_buffer_new(size_t page_count, size_t buffer_size,
				    gfp_t gfp_mask)
{
	struct diff_buffer *diff_buffer;
	size_t inx = 0;
	struct page *page;

	if (unlikely(page_count <= 0))
		return NULL;

	/*
	 * In case of overflow, it is better to get a null pointer
	 * than a pointer to some memory area. Therefore + 1.
	 */
	diff_buffer = kzalloc(sizeof(struct diff_buffer) + (page_count + 1) * sizeof(struct page *),
			      gfp_mask);
	if (!diff_buffer)
		return NULL;

#ifdef CONFIG_DEBUG_DIFF_BUFFER
	atomic_inc(&diff_buffer_allocated_counter);
	pr_debug("Allocate buffer #%d \n",
		atomic_read(&diff_buffer_allocated_counter));
#endif
	INIT_LIST_HEAD(&diff_buffer->link);
	diff_buffer->size = buffer_size;
	diff_buffer->page_count = page_count;

	for (inx = 0; inx < page_count; inx++) {
		page = alloc_page(gfp_mask);
		if (!page)
			goto fail;

		diff_buffer->pages[inx] = page;
	}
	return diff_buffer;
fail:
	diff_buffer_free(diff_buffer);
	return NULL;
}

struct diff_buffer *diff_buffer_take(struct diff_area *diff_area, gfp_t gfp_mask)
{
	struct diff_buffer *diff_buffer = NULL;
	sector_t chunk_sectors;
	size_t page_count;
	size_t buffer_size;

	spin_lock(&diff_area->free_diff_buffers_lock);
	diff_buffer = list_first_entry_or_null(&diff_area->free_diff_buffers, struct diff_buffer, link);
	if (diff_buffer) {
		list_del(&diff_buffer->link);
		atomic_dec(&diff_area->free_diff_buffers_count);
	}
	spin_unlock(&diff_area->free_diff_buffers_lock);

	/* Return free buffer if it was found in a pool */
	if (diff_buffer) {
#ifdef CONFIG_DEBUG_DIFF_BUFFER
	pr_debug("Took buffer from pool");
#endif
		return diff_buffer;
	}

	/* Allocate new buffer */
	chunk_sectors = diff_area_chunk_sectors(diff_area);
	page_count = round_up(chunk_sectors, SECTOR_IN_PAGE) / SECTOR_IN_PAGE;
	buffer_size = chunk_sectors << SECTOR_SHIFT;

	diff_buffer = diff_buffer_new(page_count, buffer_size, gfp_mask);
	if (unlikely(!diff_buffer))
		return ERR_PTR(-ENOMEM);

	return diff_buffer;
}

void diff_buffer_release(struct diff_area *diff_area, struct diff_buffer *diff_buffer)
{
	if (unlikely(!diff_buffer))
		return;

#ifdef CONFIG_DEBUG_DIFF_BUFFER
	pr_debug("Release buffer");
#endif
	if (atomic_read(&diff_area->free_diff_buffers_count) > free_diff_buffer_pool_size) {
		diff_buffer_free(diff_buffer);
		return;
	}
	spin_lock(&diff_area->free_diff_buffers_lock);
	list_add_tail(&diff_buffer->link, &diff_area->free_diff_buffers);
	atomic_inc(&diff_area->free_diff_buffers_count);
	spin_unlock(&diff_area->free_diff_buffers_lock);
}
