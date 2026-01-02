/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023 Veeam Software Group GmbH */
#ifndef __BLKSNAP_DIFF_BUFFER_H
#define __BLKSNAP_DIFF_BUFFER_H

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/blkdev.h>

struct diff_area;

/**
 * struct diff_buffer - Difference buffer.
 * @link:
 *	The list header allows to create a pool of the diff_buffer structures.
 * @size:
 *	Count of bytes in the buffer.
 * @nr_pages:
 *	The number of pages reserved for the buffer.
 * @bvec:
 *	An array of pages in bio_vec form.
 *
 * Describes the buffer in memory for a chunk.
 */
struct diff_buffer {
	struct list_head link;
	size_t size;
	unsigned long nr_pages;
	struct bio_vec bvec[];
};

struct diff_buffer *diff_buffer_take(struct diff_area *diff_area);
void diff_buffer_release(struct diff_area *diff_area,
			 struct diff_buffer *diff_buffer);
void diff_buffer_cleanup(struct diff_area *diff_area);
#endif /* __BLKSNAP_DIFF_BUFFER_H */
