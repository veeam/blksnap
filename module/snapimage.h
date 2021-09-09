/* SPDX-License-Identifier: GPL-2.0 */
#pragma once

struct diff_area;
struct cbt_map;

/**
 * struct snapimage - Snapshot image block device.
 * 
 * @image_dev_id:
 * 
 * @capacity:
 * 
 * @tag_set:
 * 	Area to keep a shared tag map.
 * @disk:
 * 
 * @diff_area:
 * 	Pointer to owned &struct diff_area.
 * @cbt_map:
 * 	Pointer to owned &struct cbt_map.
 */
struct snapimage {
	dev_t image_dev_id;
	sector_t capacity;

	struct blk_mq_tag_set tag_set;
	struct gendisk *disk;

	struct diff_area *diff_area;
	struct cbt_map *cbt_map;
};

int snapimage_init(void);
void snapimage_done(void);

void snapimage_free(struct snapimage *snapimage);
struct snapimage *snapimage_create(struct diff_area *diff_area, struct cbt_map *cbt_map);
