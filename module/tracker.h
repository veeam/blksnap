/* SPDX-License-Identifier: GPL-2.0 */
#pragma once
#include "rangevector.h"
#include "cbt_map.h"
#include "snapstore_device.h"
#include "blk_snap.h"
#include "snapshot.h"

struct tracker {
	struct list_head link;
	struct kref refcount;
	dev_t dev_id;

	atomic_t is_busy_with_snapshot;

	struct cbt_map *cbt_map;
	struct diff_area *diff_area;
};

void tracker_free(struct kref *kref);
static inline void tracker_get(struct tracker *tracker);
{
	kref_get(&tracker->refcount);
};
static inline void tracker_put(struct tracker *tracker)
{
	kref_put(&tracker->refcount, tracker_free);
};
struct tracker *tracker_get_by_dev_id(dev_t dev_id);

int tracker_capture_snapshot(dev_t *dev_id_array, int dev_id_array_size);
void tracker_release_snapshot(dev_t *dev_id_array, int dev_id_array_size);


/*
bool tracker_cbt_bitmap_lock(struct tracker *tracker);
void tracker_cbt_bitmap_unlock(struct tracker *tracker);
*/
int tracker_init(void);
void tracker_done(void);

int tracker_add(dev_t dev_id);
int tracker_remove(dev_t dev_id);
int tracker_collect(int max_count, struct blk_snap_cbt_info *cbt_info, int *p_count);
int tracker_read_cbt_bitmap(dev_t dev_id, unsigned int offset, size_t length,
			     void __user *user_buff);
int tracker_mark_dirty_blocks(dev_t dev_id, struct blk_snap_block_range *block_ranges,
				unsigned int count);
