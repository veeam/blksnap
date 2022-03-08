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
 *
 * @capacity:
 *
 * @tag_set:
 *	Area to keep a shared tag map.
 * @disk:
 *
 * @diff_area:
 *	Pointer to owned &struct diff_area.
 * @cbt_map:
 *	Pointer to owned &struct cbt_map.
 */
struct snapimage {
	dev_t image_dev_id;
	sector_t capacity;
	bool is_ready;

	struct kthread_worker worker;
	struct task_struct *worker_task;

	struct blk_mq_tag_set tag_set;
	struct gendisk *disk;
	struct request_queue *queue;

	struct diff_area *diff_area;
	struct cbt_map *cbt_map;
};

int snapimage_init(void);
void snapimage_done(void);
int snapimage_major(void);

void snapimage_free(struct snapimage *snapimage);
struct snapimage *snapimage_create(struct diff_area *diff_area,
				   struct cbt_map *cbt_map);
