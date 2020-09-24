/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "sector.h"
#include "blk_descr_file.h"
#include "blk_descr_mem.h"
#include "blk_descr_multidev.h"

#define DEFER_IO_DIO_REQUEST_LENGTH 250
#define DEFER_IO_DIO_REQUEST_SECTORS_COUNT (10 * 1024 * 1024 / SECTOR_SIZE)

struct blk_deferred_io {
	struct list_head link;

	unsigned long blk_index;
	union blk_descr_unify blk_descr;

	struct blk_range sect;

	struct page **page_array; //null pointer on tail
};

struct blk_deferred_request {
	struct completion complete;
	sector_t sect_len;
	atomic64_t sect_processed;
	int result;

	struct list_head dios;
};

void blk_deferred_done(void);

struct blk_deferred_io *blk_deferred_alloc(unsigned long block_index,
					   union blk_descr_unify blk_descr);
void blk_deferred_free(struct blk_deferred_io *dio);

void blk_deferred_bio_endio(struct bio *bio);

sector_t blk_deferred_submit_pages(struct block_device *blk_dev,
				   struct blk_deferred_request *dio_req, int direction,
				   sector_t arr_ofs, struct page **page_array, sector_t ofs_sector,
				   sector_t size_sector);

struct blk_deferred_request *blk_deferred_request_new(void);

bool blk_deferred_request_already_added(struct blk_deferred_request *dio_req,
					unsigned long block_index);

int blk_deferred_request_add(struct blk_deferred_request *dio_req, struct blk_deferred_io *dio);
void blk_deferred_request_free(struct blk_deferred_request *dio_req);
void blk_deferred_request_deadlocked(struct blk_deferred_request *dio_req);

void blk_deferred_request_waiting_skip(struct blk_deferred_request *dio_req);
int blk_deferred_request_wait(struct blk_deferred_request *dio_req);

int blk_deferred_bioset_create(void);
void blk_deferred_bioset_free(void);

int blk_deferred_request_read_original(struct block_device *original_blk_dev,
				       struct blk_deferred_request *dio_copy_req);

int blk_deferred_request_store_file(struct block_device *blk_dev,
				    struct blk_deferred_request *dio_copy_req);
#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
int blk_deferred_request_store_multidev(struct blk_deferred_request *dio_copy_req);
#endif
int blk_deffered_request_store_mem(struct blk_deferred_request *dio_copy_req);
