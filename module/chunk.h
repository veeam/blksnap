/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BLK_SNAP_CHUNK_H
#define __BLK_SNAP_CHUNK_H

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
 *	The chunk is in the dirty state. The chunk is marked dirty in case
 *	there was a write operation to the snapshot image.
 *	The flag is removed when the data of the chunk is stored in the
 *	difference storage.
 * @CHUNK_ST_BUFFER_READY:
 *	The data of the chunk is ready to be read from the RAM buffer.
 *	The flag is removed when a chunk is removed from the cache and its
 *	buffer is released.
 * @CHUNK_ST_STORE_READY:
 *	The data of the chunk has been written to the difference storage.
 *	The flag cannot be removed.
 * @CHUNK_ST_LOADING:
 *	The data is being read from the original block device.
 *	The flag is replaced with the CHUNK_ST_BUFFER_READY flag.
 * @CHUNK_ST_STORING:
 *	The data is being saved to the difference storage.
 *	The flag is replaced with the CHUNK_ST_STORE_READY flag.
 *
 * Chunks life circle.
 * Copy-on-write when writing to original:
 *	0 -> LOADING -> BUFFER_READY -> BUFFER_READY | STORING ->
 *	BUFFER_READY | STORE_READY -> STORE_READY
 * Write to snapshot image:
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
 * struct chunk - Minimum data storage unit.
 *
 * @cache_link:
 *	The list header allows to create caches of chunks.
 * @diff_area:
 *	Pointer to the difference area - the storage of changes for a specific device.
 * @number:
 *	Sequential number of the chunk.
 * @sector_count:
 *	Number of sectors in the current chunk. This is especially true
 *	for the	last chunk.
 * @lock:
 *	Binary semaphore. Syncs access to the chunks fields: state,
 *	diff_buffer, diff_region and diff_io.
 * @state:
 *	Defines the state of a chunk. May contain CHUNK_ST_* bits.
 * @diff_buffer:
 *	Pointer to &struct diff_buffer. Describes a buffer in the memory
 *	for storing the chunk data.
 * @diff_region:
 *	Pointer to &struct diff_region. Describes a copy of the chunk data
 *	on the difference storage.
 * @diff_io:
 *	Provides I/O operations for a chunk.
 *
 * This structure describes the block of data that the module operates
 * with when executing the copy-on-write algorithm and when performing I/O
 * to snapshot images.
 *
 * If the data of the chunk has been changed or has just been read, then
 * the chunk gets into cache.
 *
 * The semaphore is blocked for writing if there is no actual data in the
 * buffer, since a block of data is being read from the original device or
 * from a diff storage. If data is being read from or written to the
 * diff_buffer, the semaphore must be locked.
 */
struct chunk {
	struct list_head cache_link;
	struct diff_area *diff_area;

	unsigned long number;
	sector_t sector_count;

	struct semaphore lock;

	atomic_t state;
	struct diff_buffer *diff_buffer;
	struct diff_region *diff_region;
	struct diff_io *diff_io;
};

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
#endif /* __BLK_SNAP_CHUNK_H */
