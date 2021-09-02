/* SPDX-License-Identifier: GPL-2.0 */
#pragma once


struct snapshot_event {
	struct list_head link;
	ktime_t time;
	int code;
	size_t data_size;
	char data[1]; /* up to PAGE_SIZE - sizeof(struct blk_snap_snapshot_event) */
};

struct snapshot {
	struct list_head link;
	struct kref kref;
	uuid_t id;

	struct rw_semaphore lock;
	struct diff_storage *diff_storage;
	struct list_head events;
	spinlock_t events_lock;

	int count;
	struct tracker *tracker_array;
	struct snapimage *snapimage_array;
};

void snapshot_done(void);

int snapshot_create(dev_t *dev_id_array, unsigned int count, uuid_t *id);
int snapshot_destroy(uuid_t *id);
int snapshot_append_storage(uuid_t *id, dev_t dev_id, sector_t sector, sector_t count);
int snapshot_take(uuid_t *id);
struct snapshot_event *snapshot_wait_event(uuid_t *id, unsigned long timeout_ms);
int snapshot_collect_images(uuid_t *id,
			    struct blk_snap_image_info __user *image_info_array,
			    unsigned int *pcount);

int snapshot_generate_event(struct snapshot *snapshot, int code,
			    const void *data, int data_size);
int snapshot_generate_msg(struct snapshot *snapshot, int code,
			  const char *fmt, ...);
