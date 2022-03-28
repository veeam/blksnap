/* SPDX-License-Identifier: GPL-2.0 */
#pragma once
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/blkdev.h>

struct diff_area;

/**
 * struct diff_buffer - Difference buffer.
 * @link:
 *	The list header allows to create pool of the diff_buffer structures.
 * @size:
 *      Count of bytes in the buffer.
 * @page_count:
 *	The number of pages reserved for the buffer.
 * @pages:
 *	An array of pointers to pages.
 *
 * Describes the memory buffer for a chunk in the memory.
 */
struct diff_buffer {
	struct list_head link;
	size_t size;
	size_t page_count;
	struct page *pages[0];
};

/**
 * struct diff_buffer_iter - Iterator for &struct diff_buffer.
 * @page:
 *      A pointer to the current page.
 * @offset:
 *      The offset in bytes in the current page.
 * @bytes:
 *      The number of bytes that can be read or written from the current page.
 *
 * It is convenient to use when copying data from or to &struct bio_vec.
 */
struct diff_buffer_iter {
	struct page *page;
	size_t offset;
	size_t bytes;
};

#define SECTOR_IN_PAGE (1 << (PAGE_SHIFT - SECTOR_SHIFT))

static inline bool diff_buffer_iter_get(struct diff_buffer *diff_buffer,
					sector_t ofs,
					struct diff_buffer_iter *iter)
{
	size_t page_inx;

	if (diff_buffer->size <= (ofs << SECTOR_SHIFT))
		return false;

	page_inx = ofs >> (PAGE_SHIFT - SECTOR_SHIFT);

	iter->page = diff_buffer->pages[page_inx];
	iter->offset = (size_t)(ofs & (SECTOR_IN_PAGE - 1)) << SECTOR_SHIFT;
	/*
	 * The size cannot exceed the size of the page, taking into account
	 * the offset in this page.
	 * But at the same time it is unacceptable to go beyond the allocated
	 * buffer.
	 */
	iter->bytes = min_t(size_t, (PAGE_SIZE - iter->offset),
			    (diff_buffer->size - (ofs << SECTOR_SHIFT)));

	return true;
};

struct diff_buffer *diff_buffer_take(struct diff_area *diff_area,
				     const bool is_nowait);
void diff_buffer_release(struct diff_area *diff_area,
			 struct diff_buffer *diff_buffer);
void diff_buffer_cleanup(struct diff_area *diff_area);
