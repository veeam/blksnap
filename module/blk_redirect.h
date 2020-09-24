#pragma once
#include "rangevector.h"

int blk_redirect_bioset_create(void);
void blk_redirect_bioset_free(void);

void blk_redirect_bio_endio(struct bio *bb);

typedef struct blk_redirect_bio_list_s {
	struct blk_redirect_bio_list_s *next;
	struct bio *this;
} blk_redirect_bio_list_t;

typedef struct redirect_bio_queue_s {
	struct list_head list;
	spinlock_t lock;

	atomic_t active_state;
	atomic_t in_queue_cnt;
	atomic_t alloc_cnt;
} redirect_bio_queue_t;

typedef void(redirect_bio_endio_complete_cb)(void *complete_param, struct bio *rq, int err);

typedef struct redirect_bio_s {
	struct list_head link;
	redirect_bio_queue_t *queue;

	struct bio *bio;
	int err;
	blk_redirect_bio_list_t *bio_list_head; //list of created bios
	atomic64_t bio_count;

	void *complete_param;
	redirect_bio_endio_complete_cb *complete_cb;
} blk_redirect_bio_t;

int blk_dev_redirect_part(blk_redirect_bio_t *rq_redir, int direction, struct block_device *blk_dev,
			  sector_t target_pos, sector_t rq_ofs, sector_t rq_count);
void blk_dev_redirect_submit(blk_redirect_bio_t *rq_redir);

int blk_dev_redirect_memcpy_part(blk_redirect_bio_t *rq_redir, int direction, void *src_buff,
				 sector_t rq_ofs, sector_t rq_count);
int blk_dev_redirect_zeroed_part(blk_redirect_bio_t *rq_redir, sector_t rq_ofs, sector_t rq_count);

int blk_dev_redirect_read_zeroed(blk_redirect_bio_t *rq_redir, struct block_device *blk_dev,
				 sector_t rq_pos, sector_t blk_ofs_start, sector_t blk_ofs_count,
				 rangevector_t *zero_sectors);

void blk_redirect_complete(blk_redirect_bio_t *rq_redir, int res);

void redirect_bio_queue_init(redirect_bio_queue_t *queue);

blk_redirect_bio_t *redirect_bio_queue_new(redirect_bio_queue_t *queue);

void redirect_bio_queue_free(blk_redirect_bio_t *rq_redir);

int redirect_bio_queue_push_back(redirect_bio_queue_t *queue, blk_redirect_bio_t *rq_redir);

blk_redirect_bio_t *redirect_bio_queue_get_first(redirect_bio_queue_t *queue);

bool redirect_bio_queue_active(redirect_bio_queue_t *queue, bool state);

#define redirect_bio_queue_empty(queue) (atomic_read(&(queue).in_queue_cnt) == 0)

#define redirect_bio_queue_unactive(queue)                                                         \
	((atomic_read(&((queue).active_state)) == false) &&                                        \
	 (atomic_read(&((queue).alloc_cnt)) == 0))
