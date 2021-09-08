/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once


struct diff_area;
struct diff_store;

/**
 * struct diff_buffer - Difference buffer
 * Describes the memory buffer for chunk in memory.
 */
struct diff_buffer {
	size_t size;
	struct page_list pages[0];
};

struct diff_buffer_iter {
	struct page *page;
	size_t offset;
	size_t bytes;
};

#define SECTOR_IN_PAGE (1 << (PAGE_SHIFT - SECTOR_SHIFT))

static inline
bool diff_buffer_iter_get(struct diff_buffer *buf, sector_t ofs, struct diff_buffer_iter *iter)
{
	size_t page_inx = ofs >> PAGE_SHIFT;

	iter->page = buf->pages[page_inx].page;
	iter->offset = (size_t)(ofs & (SECTOR_IN_PAGE - 1)) << SECTOR_SHIFT;
	iter->bytes = buf->size - iter->offset;

	return !!iter->bytes;
};

enum {
	CHUNK_ST_FAILED,	/* An error occurred while processing the chunks data */
	CHUNK_ST_DIRTY,		/* The data on the original device and the snapshot image differ in this chunk */
	CHUNK_ST_BUFFER_READY,	/* The data of the chunk is ready to be read from the RAM buffer */
	CHUNK_ST_STORE_READY,	/* The data of the chunk was wrote to the difference storage */
};

struct chunk {
	struct list_head link;
	//struct kref kref;
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

	atomic_t state;
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

	struct diff_buffer *diff_buffer;
	struct diff_store *diff_store;
};

unsigned long long chunk_calculate_optimal_size_shift(struct block_device *bdev);

static inline
void chunk_state_set(struct chunk* chunk, int st)
{
	atomic_or(&chunk->state, (1 << st));
};

static inline
void chunk_state_unset(struct chunk* chunk, int st)
{
	atomic_and(&chunk->state, ~(1 << st));
};

static inline
bool chunk_state_check(struct chunk* chunk, int st)
{
	return atomic_read(chunk->state) & (1 << st);
};

struct chunk *chunk_alloc(struct diff_area *diff_area, unsigned long number);
void chunk_free(struct chunk *chunk);
/*void chunk_free(struct kref *kref);
static inline void chunk_get(struct chunk *chunk)
{
	kref_get(&chunk->kref);
};
static inline void chunk_put(struct chunk *chunk)
{
	if (likely(chunk))
		kref_put(&chunk->kref, chunk_free);
};*/

int chunk_allocate_buffer(struct chunk *chunk, gfp_t gfp_mask);

static inline void chunk_io_failed(struct chunk *chunk, int error)
{
	chunk_state_set(chunk, CHUNK_ST_FAILED);
	diff_area_set_corrupted(chunk->diff_area, error);
	pr_err("Failed to write chunk %lu\n", chunk->number);
};

/* Asynchronous operations are used to implement the COW algorithm. */
int chunk_async_store_diff(struct chunk *chunk, io_notify_fn *fn);
int chunk_asunc_load_orig(struct chunk *chunk, io_notify_fn *fn);

/* Synchronous operations are used to implement reading and writing to the snapshot image. */
int chunk_load_orig(struct chunk *chunk);
int chunc_load_diff(struct chunk *chunk);
