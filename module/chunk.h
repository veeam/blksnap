/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <linux/blk_types.h>
#include <linux/blkdev.h>
#include <linux/rwsem.h>
#include <linux/atomic.h>
#include <linux/dm-io.h>

struct diff_area;
struct diff_store;

enum {
	CHUNK_ST_FAILED,	/* An error occurred while processing the chunks data */
	CHUNK_ST_DIRTY,		/* The data on the original device and the snapshot image differ in this chunk */
	CHUNK_ST_BUFFER_READY,	/* The data of the chunk is ready to be read from the RAM buffer */
	CHUNK_ST_STORE_READY,	/* The data of the chunk was wrote to the difference storage */
	CHUNK_ST_IN_CACHE,      /* The chunk in the cache in the queue for release. */
	CHUNK_ST_LOADING,
	CHUNK_ST_STORING,
};

/**
 * struct chunk - Elementary IO block.
 * @link:
 * 	?
 * @diff_area:
 * 	?
 * @number:
 * 	Sequential number of chunk.
 * @sector_count:
 * 	Numbers of sectors in current chunk this is especially true for the
 * 	last piece.
 * @state:
 * 	?
 * @lock:
 * 	Syncs access to the chunks fields: state, diff_buffer and diff_store.
 * 	The semaphore is blocked for writing if there is no actual data
 * 	in the buffer, since a block of data is being read from the original
 * 	device or from a diff storage.
 * 	If data is being read or written from the chunk buffer, the semaphore
 * 	must be blocked for reading.
 * 	The module does not prohibit reading and writing data to the snapshot
 * 	from different threads in parallel.
 * 	To avoid the problem with simultaneous access, it is enough to open
 * 	the snapshot image block device with the FMODE_EXCL parameter.
 * @diff_buffer:
 * 	Pointer to &struct diff_buffer. Describes a buffer in memory for
 * 	storing chunk data.
 * @diff_store:
 * 	Pointer to &struc diff_store. Describes a copy of the chunk data
 * 	on the storage.
 * This structure describes the block of data that the module operates with
 * when executing the COW algorithm and when performing IO to snapshot images.
 */
struct chunk {
	struct list_head cache_link;
	struct diff_area *diff_area;

	unsigned long number;
	sector_t sector_count;
	atomic_t state;

	struct mutex lock;

	int error;
	struct work_struct notify_work;

	struct diff_buffer *diff_buffer;
	struct diff_store *diff_store;
};

unsigned long long chunk_calculate_optimal_size_shift(struct block_device *bdev);

static inline
void chunk_state_set(struct chunk* chunk, int st)
{
	atomic_or((1 << st), &chunk->state);
};

static inline
void chunk_state_unset(struct chunk* chunk, int st)
{
	atomic_and(~(1 << st), &chunk->state);
};

static inline
bool chunk_state_check(struct chunk* chunk, int st)
{
	return !!(atomic_read(&chunk->state) & (1 << st));
};

struct chunk *chunk_alloc(struct diff_area *diff_area, unsigned long number);
void chunk_free(struct chunk *chunk);

void chunk_schedule_storing(struct chunk *chunk);
void chunk_schedule_caching(struct chunk *chunk);

/* Asynchronous operations are used to implement the COW algorithm. */
int chunk_async_store_diff(struct chunk *chunk);
int chunk_asunc_load_orig(struct chunk *chunk);

/* Synchronous operations are used to implement reading and writing to the snapshot image. */
int chunk_load_orig(struct chunk *chunk);
int chunk_load_diff(struct chunk *chunk);
