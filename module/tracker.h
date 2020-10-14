/* SPDX-License-Identifier: GPL-2.0 */
#pragma once
#include "cbt_map.h"
#include "defer_io.h"
#include "blk-snap-ctl.h"
#include "snapshot.h"

struct tracker {
	struct list_head link;
	dev_t original_dev_id;
	struct block_device *target_dev;
	struct cbt_map *cbt_map;
	atomic_t is_captured;
	struct defer_io *defer_io;
	unsigned long long snapshot_id; // current snapshot for this device
};

void tracker_done(void);

int tracker_find_by_bio(struct bio *bio, struct tracker **ptracker);
int tracker_find_by_dev_id(dev_t dev_id, struct tracker **ptracker);

int tracker_enum_cbt_info(int max_count, struct cbt_info_s *p_cbt_info, int *p_count);

int tracker_capture_snapshot(dev_t *dev_id_set, int dev_id_set_size);
void tracker_release_snapshot(dev_t *dev_id_set, int dev_id_set_size);

int _tracker_create(struct tracker *tracker, void *filter, bool attach_filter);
int tracker_create(dev_t dev_id, void *filter, struct tracker **ptracker);

void _tracker_remove(struct tracker *tracker, bool detach_filter);
void tracker_remove(struct tracker *tracker);
void tracker_remove_all(void);

void tracker_cbt_bitmap_set(struct tracker *tracker, sector_t sector, sector_t sector_cnt);

bool tracker_cbt_bitmap_lock(struct tracker *tracker);
void tracker_cbt_bitmap_unlock(struct tracker *tracker);
