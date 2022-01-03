/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <linux/blk_types.h>
#include <linux/blkdev.h>
#include <linux/rwsem.h>
#include <linux/atomic.h>

struct diff_area;
struct diff_region;
struct diff_io;


#define CHUNK_ST_FAILED         (1 << 0) /* An error occurred while processing the chunks data */
#define CHUNK_ST_DIRTY          (1 << 1) /* The data on the original device and the snapshot image differ in this chunk */
#define CHUNK_ST_BUFFER_READY   (1 << 2) /* The data of the chunk is ready to be read from the RAM buffer */
#define CHUNK_ST_STORE_READY    (1 << 3) /* The data of the chunk was wrote to the difference storage */
#define CHUNK_ST_IN_CACHE       (1 << 4) /* The chunk in the cache in the queue for release. */
#define CHUNK_ST_LOADING        (1 << 5)
#define CHUNK_ST_STORING        (1 << 6)

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
 * 	Syncs access to the chunks fields: state, diff_buffer and diff_region.
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
 * @diff_region:
 * 	Pointer to &struct diff_region. Describes a copy of the chunk data
 * 	on the difference storage.
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

	struct diff_buffer *diff_buffer;
	struct diff_region *diff_region;
	struct diff_io *diff_io;
};

unsigned long long chunk_calculate_optimal_size_shift(struct block_device *bdev);

static inline
void chunk_state_set(struct chunk* chunk, int st)
{
	atomic_or(st, &chunk->state);
};

static inline
void chunk_state_unset(struct chunk* chunk, int st)
{
	atomic_and(~st, &chunk->state);
};

static inline
bool chunk_state_check(struct chunk* chunk, int st)
{
	return !!(atomic_read(&chunk->state) & st);
};

struct chunk *chunk_alloc(struct diff_area *diff_area, unsigned long number);
void chunk_free(struct chunk *chunk);

int chunk_schedule_storing(struct chunk *chunk, bool is_nowait);
void chunk_store_failed(struct chunk *chunk, int error);

void chunk_schedule_caching(struct chunk *chunk);

/* Asynchronous operations are used to implement the COW algorithm. */
int chunk_async_store_diff(struct chunk *chunk, bool is_nowait);
int chunk_asunc_load_orig(struct chunk *chunk, bool is_nowait);

/* Synchronous operations are used to implement reading and writing to the snapshot image. */
int chunk_load_orig(struct chunk *chunk);
int chunk_load_diff(struct chunk *chunk);
