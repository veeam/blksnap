/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <linux/blk_types.h>
#include <linux/blkdev.h>
#include <linux/rwsem.h>
#include <linux/atomic.h>

struct diff_area;
struct diff_region;
struct diff_io;

/**
 * enum chunk_st - Possible states for a chunk.
 *
 * @CHUNK_ST_FAILED:
 *	An error occurred while processing the chunk data.
 * @CHUNK_ST_DIRTY:
 *	The chunk is in the dirty state. The chunk is marked dirty in case there 
 *  was a write operation to the snapshot image.
 * @CHUNK_ST_BUFFER_READY:
 *	The data of the chunk is ready to be read from the RAM buffer.
 * @CHUNK_ST_STORE_READY:
 *	The data of the chunk has been written to the difference storage.
 * @CHUNK_ST_LOADING:
 *	The data is being read from the original block device.
 * @CHUNK_ST_STORING:
 *	The data is being saved to the difference storage.
 *
 * Chunks life circle.
 * COW on write to original:
 *	0 -> LOADING -> BUFFER_READY -> BUFFER_READY | STORING ->
 *	BUFFER_READY | STORE_READY -> STORE_READY
 * Write to snapshot image:
 *	[TBD] It would be necessary to double-check the diagram
 *	0 -> LOADING -> BUFFER_READY | DIRTY -> DIRTY | STORING ->
 *	BUFFER_READY | STORE_READY -> STORE_READY
 */
enum chunk_st {
	CHUNK_ST_FAILED = (1 << 0),
	CHUNK_ST_DIRTY = (1 << 1),
	CHUNK_ST_BUFFER_READY = (1 << 2),
	CHUNK_ST_STORE_READY = (1 << 3),
	CHUNK_ST_LOADING = (1 << 4),
	CHUNK_ST_STORING = (1 << 5),
};

/**
 * struct chunk - Elementary copy block.
 *
 * @cache_link:
 *	If the data of a chunk has been changed or has been just read,
 *  then the chunk gets into a temporary buffer.
 * @diff_area:
 *  Describes the storage of changes for a specific device.
 * @number:
 *	Sequential number of the chunk.
 * @sector_count:
 *	Number of sectors in the current chunk. This is especially true for the
 *	last chunk.
 * @state:
 *	Defines the state of a chunk. May contain CHUNK_ST_* bits.
 * @lock:
 *	Syncs access to the chunk fields: state, diff_buffer and diff_region.
 *	The semaphore is blocked for writing if there is no actual data
 *	in the buffer, since a block of data is being read from the original
 *	device or from a difference storage.
 *	If data is being read or written from the chunk buffer, the semaphore
 *	must be blocked for reading.
 *	The module does not prohibit reading and writing data to the snapshot
 *	from different threads in parallel.
 *	To avoid the problem with simultaneous access, it is enough to open
 *	the snapshot image block device with the FMODE_EXCL parameter.
 * @diff_buffer:
 *	Pointer to &struct diff_buffer. Describes a buffer in the memory for
 *	storing the chunk data.
 * @diff_region:
 *	Pointer to &struct diff_region. Describes a copy of the chunk data
 *	on the difference storage.
 * @diff_io:
 *	Provides I/O operations for a chunk.
 *
 * This structure describes the block of data the module interacts with
 * when executing the COW algorithm and when performing I/O to snapshot images.
 */
struct chunk {
	struct list_head cache_link;
	struct diff_area *diff_area;

	unsigned long number;
	atomic_t state;
	sector_t sector_count;

	struct semaphore lock;

	struct diff_buffer *diff_buffer;
	struct diff_region *diff_region;
	struct diff_io *diff_io;
};

unsigned long long
chunk_calculate_optimal_size_shift(struct block_device *bdev);

static inline void chunk_state_set(struct chunk *chunk, int st)
{
	atomic_or(st, &chunk->state);
};

static inline void chunk_state_unset(struct chunk *chunk, int st)
{
	atomic_and(~st, &chunk->state);
};

static inline bool chunk_state_check(struct chunk *chunk, int st)
{
	return !!(atomic_read(&chunk->state) & st);
};

struct chunk *chunk_alloc(struct diff_area *diff_area, unsigned long number);
void chunk_free(struct chunk *chunk);

int chunk_schedule_storing(struct chunk *chunk, bool is_nowait);
void chunk_diff_buffer_release(struct chunk *chunk);
void chunk_store_failed(struct chunk *chunk, int error);

void chunk_schedule_caching(struct chunk *chunk);

/* Asynchronous operations are used to implement the COW algorithm. */
int chunk_async_store_diff(struct chunk *chunk, bool is_nowait);
int chunk_async_load_orig(struct chunk *chunk, const bool is_nowait);

/* Synchronous operations are used to implement reading and writing to the snapshot image. */
int chunk_load_orig(struct chunk *chunk);
int chunk_load_diff(struct chunk *chunk);
