/* SPDX-License-Identifier: GPL-2.0 */
#pragma once


struct snapshot_event {
	struct list_head link;
	ktime_t time;
	int code;
	char data[0]; /* up to PAGE_SIZE - sizeof(struct blk_snap_snapshot_event) */
};

struct snapshot {
	struct list_head link;
	uuid_t *id;

	struct rw_semaphore lock;
	struct diff_storage *diff_storage;
	struct list_head events;
	struct tracker *tracker_array;
	struct snapimage *snapimage_array;
};

void snapshot_done(void);

int snapshot_create(dev_t *dev_id_array, unsigned int dev_id_array_size, uuid_t *id);

int snapshot_destroy(uuid_t *id, );

int snapshot_append_storage(uuid_t *id, dev_t dev_id, sector_t sector, sector_t count);

struct snapshot_event * snapshot_wait_event(uuid_t *id, unsigned long timeout_ms);
