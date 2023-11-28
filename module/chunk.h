/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023 Veeam Software Group GmbH */
#ifndef __BLKSNAP_CHUNK_H
#define __BLKSNAP_CHUNK_H

#include <linux/blk_types.h>
#include <linux/blkdev.h>
#include <linux/rwsem.h>
#include <linux/atomic.h>
#include "diff_area.h"

struct diff_area;

/**
 * enum chunk_st - Possible states for a chunk.
 *
 * @CHUNK_ST_NEW:
 *	No data is associated with the chunk.
 * @CHUNK_ST_IN_MEMORY:
 *	The data of the chunk is ready to be read from the RAM buffer.
 *	The flag is removed when a chunk is removed from the store queue
 *	and its buffer is released.
 * @CHUNK_ST_STORED:
 *	The data of the chunk has been written to the difference storage.
 * @CHUNK_ST_FAILED:
 *	An error occurred while processing the chunk data.
 *
 * Chunks life circle:
 *	CHUNK_ST_NEW -> CHUNK_ST_IN_MEMORY <-> CHUNK_ST_STORED
 */

enum chunk_st {
	CHUNK_ST_NEW,
	CHUNK_ST_IN_MEMORY,
	CHUNK_ST_STORED,
	CHUNK_ST_FAILED,
};

/**
 * struct chunk - Minimum data storage unit.
 *
 * @link:
 *	The list header allows to create queue of chunks.
 * @number:
 *	Sequential number of the chunk.
 * @sector_count:
 *	Number of sectors in the current chunk. This is especially true
 *	for the	last chunk.
 * @lock:
 *	Binary semaphore. Syncs access to the chunks fields: state,
 *	diff_buffer, diff_file and diff_ofs_sect.
 * @diff_area:
 *	Pointer to the difference area - the difference storage area for a
 *	specific device. This field is only available when the chunk is locked.
 *	Allows to protect the difference area from early release.
 * @state:
 *	Defines the state of a chunk.
 * @diff_bdev:
 *      The difference storage block device.
 * @diff_file:
 *	The difference storage file.
 * @diff_ofs_sect:
 *	The sector offset of the region's first sector.
 * @diff_buffer:
 *	Pointer to &struct diff_buffer. Describes a buffer in the memory
 *	for storing the chunk data.
 *	on the difference storage.
 *
 * This structure describes the block of data that the module operates
 * with when executing the copy-on-write algorithm and when performing I/O
 * to snapshot images.
 *
 * If the data of the chunk has been changed, then the chunk gets into store
 * queue. The queue provides caching of chunks. Saving chunks to the storage is
 * performed in a separate working thread. This ensures the best system
 * performance.
 *
 * The semaphore is blocked for writing if there is no actual data in the
 * buffer, since a block of data is being read from the original device or
 * from a difference storage. If data is being read from or written to the
 * diff_buffer, the semaphore must be locked.
 */
struct chunk {
	struct list_head link;
	unsigned long number;
	sector_t sector_count;

	struct semaphore lock;
	struct diff_area *diff_area;

	enum chunk_st state;

#if defined(CONFIG_BLKSNAP_DIFF_BLKDEV)
	struct block_device *diff_bdev;
#endif
	struct file *diff_file;
	sector_t diff_ofs_sect;

	struct diff_buffer *diff_buffer;
};

static inline void chunk_up(struct chunk *chunk)
{
	struct diff_area *diff_area = chunk->diff_area;

	chunk->diff_area = NULL;
	up(&chunk->lock);
	diff_area_put(diff_area);
};

struct chunk_io_ctx {
	struct list_head link;
#ifdef CONFIG_BLKSNAP_CHUNK_DIFF_BIO_SYNC
	loff_t pos;
#else
	struct kiocb iocb;
#endif
	struct iov_iter iov_iter;
	struct chunk *chunk;
	struct bio *bio;
};
void chunk_diff_bio_execute(struct chunk_io_ctx *io_ctx);

void chunk_store_failed(struct chunk *chunk, int error);
struct bio *chunk_alloc_clone(struct block_device *bdev, struct bio *bio);

void chunk_copy_bio(struct chunk *chunk, struct bio *bio,
		    struct bvec_iter *iter);
#if defined(CONFIG_BLKSNAP_DIFF_BLKDEV)
void chunk_diff_bio_tobdev(struct chunk *chunk, struct bio *bio);
void chunk_store_tobdev(struct chunk *chunk);
#endif
int chunk_diff_bio(struct chunk *chunk, struct bio *bio);
void chunk_diff_write(struct chunk *chunk);
bool chunk_load_and_schedule_io(struct chunk *chunk, struct bio *orig_bio);
int chunk_load_and_postpone_io(struct chunk *chunk, struct bio **chunk_bio);
void chunk_load_and_postpone_io_finish(struct list_head *chunks,
				struct bio *chunk_bio, struct bio *orig_bio);

int __init chunk_init(void);
void chunk_done(void);
#endif /* __BLKSNAP_CHUNK_H */
