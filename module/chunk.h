/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once


struct diff_area;
struct diff_store;

/**
 * struct diff_buffer - Difference buffer
 * Describes the memory buffer for chunk in memory.
 */
struct diff_buffer {
	struct kref kref;
	struct page_list pages[0];
};

void diff_buffer_free(struct kref *kref)
struct diff_buffer *diff_buffer_new(size_t page_count);
void diff_buffer_get(struct diff_buffer *diff_buffer)
{
	kref_get(diff_buffer->kref);
};
void diff_buffer_put(struct diff_buffer *diff_buffer)
{
	kref_put(diff_buffer->kref, diff_buffer_free);
};


enum {
	CHUNK_ST_FAILED,	/* An error occurred while processing the chunks data */
	CHUNK_ST_DIRTY,		/* The data on the original device and the snapshot image differ in this chunk */
	CHUNK_ST_BUFFER_READY,	/* The data of the chunk is ready to be read from the RAM buffer */
	CHUNK_ST_STORE_READY,	/* The data of the chunk was wrote to the difference storage */
};

struct chunk {
	struct list_head link;
	struct kref refcount;
	struct diff_area *diff_area;
	/**
	 * sequential number of chunk
	 */
	unsigned long number;
	/**
	 * numbers of sectors in current chunk
	 * this is especially true for the last piece.
	 */
	sector_t sector_count; 
	/**
	 * lock - syncs access to the chunks fields: state, diff_buffer and
	 * diff_store.
	 * The semaphore is blocked for writing if there is no actual data
	 * in the buffer, since a block of data is being read from the original
	 * device or from a diff storage.
	 * If data is being read or written from the chunk buffer, the semaphore
	 * must be blocked for reading.
	 * The module does not prohibit reading and writing data to the snapshot
	 * from different threads in parallel.
	 * To avoid the problem with simultaneous access, it is enough to open
	 * the snapshot image block device with the FMODE_EXCL parameter.
	 */
	struct rw_semaphore lock; 
	int state;
	struct diff_buffer *diff_buffer;
	struct diff_store *diff_store;
};

unsigned long long chunk_calculate_optimal_size_shift(struct block_device *bdev);

static inline void chunk_state_set(struct chunk* chunk, int st)
{
	chunk->state |= (1 << st);
};

static inline void chunk_state_unset(struct chunk* chunk, int st)
{
	chunk->state &= ~(1 << st);
};

static inline bool chunk_state_check(struct chunk* chunk, int st)
{
	return (chunk->state & (1 << st));
};

struct chunk *chunk_alloc(struct diff_area *diff_area, unsigned long number);
void chunk_free(struct kref *kref);
static inline void chunk_get(struct chunk *chunk)
{
	kref_get(&chunk->refcount);
};
static inline void chunk_put(struct chunk *chunk)
{
	kref_put(&chunk->refcount, chunk_free);
};



void chunk_cache_store(struct chunk *chunk);
void chunk_cache_del(struct chunk *chunk);
void chunk_cache_touch(struct chunk *chunk);
void chunk_cache_flush(struct chunk *chunk);
