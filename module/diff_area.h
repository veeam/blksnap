/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once


struct diff_area;
struct diff_storage;

struct diff_area *diff_area_new(dev_t dev_id, struct diff_storage *diff_storage);
void diff_area_free(struct kref *kref);
static inline void diff_area_get(struct diff_area *diff_area)
{
	kref_get(diff_area->kref);
};
static inline void diff_area_put(struct diff_area *diff_area)
{
	kref_put(diff_area->kref, diff_area_free);
};

int diff_area_copy(struct diff_area *diff_area, sector_t sector, sector_t count
                   bool is_nowait);
blk_status_t diff_area_image_write(struct diff_area *diff_area,
				   struct page *page, unsigned int page_off,
				   sector_t sector, unsigned int len);
blk_status_t diff_area_image_read(struct diff_area *diff_area,
				  struct page *page, unsigned int page_off,
				  sector_t sector, unsigned int len);
