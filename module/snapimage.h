/* SPDX-License-Identifier: GPL-2.0 */
#pragma once
#include <linux/blk_types.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/genhd.h>
#include <linux/kthread.h>

struct diff_area;
struct cbt_map;

/**
 * struct snapimage - Snapshot image block device.
 *
 * @image_dev_id:
 *	ID of the snapshot image block device.
 * @capacity:
 *	The size of the snapshot image in sectors must be equal to the size
 *	of the original device at the time of taking the snapshot.
 * @is_ready:
 *	The flag means that the snapshot image is ready for processing
 *	I/O requests.
 * @worker:
 *	The worker thread for processing I/O requests.
 * @worker_task:
 *	A pointer to the &struct task of the worker thread.
 * @tag_set:
 *	Area to keep a shared tag map.
 * @disk:
 *	A pointer to &struct gendisk for image block device.
 * @diff_area:
 *	A pointer to owned &struct diff_area.
 * @cbt_map:
 *	A pointer to owned &struct cbt_map.
 *
 * The snapshot image is presented in the system as a block device. But
 * when reading or writing a snapshot image, the data is redirected to
 * the original block device or to the block device of the difference storage.
 *
 * The module does not prohibit reading and writing data to the snapshot
 * from different threads in parallel. To avoid the problem with simultaneous
 * access, it is enough to open the snapshot image block device with the
 * FMODE_EXCL parameter.
 */
struct snapimage {
	dev_t image_dev_id;
	sector_t capacity;
	bool is_ready;

	struct kthread_worker worker;
	struct task_struct *worker_task;

	struct blk_mq_tag_set tag_set;
	struct gendisk *disk;

	struct diff_area *diff_area;
	struct cbt_map *cbt_map;
};

int snapimage_init(void);
void snapimage_done(void);
int snapimage_major(void);

void snapimage_free(struct snapimage *snapimage);
struct snapimage *snapimage_create(struct diff_area *diff_area,
				   struct cbt_map *cbt_map);
