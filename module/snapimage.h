/* SPDX-License-Identifier: GPL-2.0 */
#pragma once

#include "blk_snap.h"

struct snapimage {
	struct kref kref;
	dev_t image_dev_id;
	sector_t capacity;

	/* Area to keep a shared tag map */
	struct blk_mq_tag_set tag_set;
	//struct request_queue *queue;
	struct gendisk *disk;

	struct diff_area *diff_area;
	struct cbt_map *cbt_map;
};

int snapimage_init(void);
void snapimage_done(void);

void snapimage_free(struct kref *kref);
struct snapimage *snapimage_create(struct diff_area *diff_area, struct cbt_map *cbt_map);

static inline void snapimage_get(struct snapimage *snapimage)
{
	kref_get(&snapimage->kref);
};
static inline void snapimage_put(struct snapimage *snapimage)
{
	if (likely(snapimage))
		kref_put(&snapimage->kref, snapimage_free);
};
