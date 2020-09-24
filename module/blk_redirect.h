/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "rangevector.h"

int blk_redirect_bioset_create(void);
void blk_redirect_bioset_free(void);

void blk_redirect_bio_endio(struct bio *bb);

struct blk_redirect_bio_list {
	struct blk_redirect_bio_list *next;
	struct bio *this;
};

struct redirect_bio_queue {
	struct list_head list;
	spinlock_t lock;

	atomic_t active_state;
	atomic_t in_queue_cnt;
	atomic_t alloc_cnt;
};

struct blk_redirect_bio {
	struct list_head link;
	struct redirect_bio_queue *queue;

	struct bio *bio;
	int err;
	struct blk_redirect_bio_list *bio_list_head; //list of created bios
	atomic64_t bio_count;

	void *complete_param;
	void (*complete_cb)(void *complete_param, struct bio *rq, int err);
};

int blk_dev_redirect_part(struct blk_redirect_bio *rq_redir, int direction,
			  struct block_device *blk_dev, sector_t target_pos, sector_t rq_ofs,
			  sector_t rq_count);

void blk_dev_redirect_submit(struct blk_redirect_bio *rq_redir);

int blk_dev_redirect_memcpy_part(struct blk_redirect_bio *rq_redir, int direction, void *src_buff,
				 sector_t rq_ofs, sector_t rq_count);

int blk_dev_redirect_zeroed_part(struct blk_redirect_bio *rq_redir, sector_t rq_ofs,
				 sector_t rq_count);

int blk_dev_redirect_read_zeroed(struct blk_redirect_bio *rq_redir, struct block_device *blk_dev,
				 sector_t rq_pos, sector_t blk_ofs_start, sector_t blk_ofs_count,
				 struct rangevector *zero_sectors);

void blk_redirect_complete(struct blk_redirect_bio *rq_redir, int res);

void redirect_bio_queue_init(struct redirect_bio_queue *queue);

struct blk_redirect_bio *redirect_bio_queue_new(struct redirect_bio_queue *queue);

void redirect_bio_queue_free(struct blk_redirect_bio *rq_redir);

int redirect_bio_queue_push_back(struct redirect_bio_queue *queue,
				 struct blk_redirect_bio *rq_redir);

struct blk_redirect_bio *redirect_bio_queue_get_first(struct redirect_bio_queue *queue);

bool redirect_bio_queue_active(struct redirect_bio_queue *queue, bool state);

#define redirect_bio_queue_empty(queue) (atomic_read(&(queue).in_queue_cnt) == 0)

#define redirect_bio_queue_unactive(queue)                                                         \
	((atomic_read(&((queue).active_state)) == false) &&                                        \
	 (atomic_read(&((queue).alloc_cnt)) == 0))
