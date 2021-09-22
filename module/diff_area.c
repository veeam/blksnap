// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME "-diff-area: " fmt
#include <linux/genhd.h>
#include <linux/slab.h>
#include "params.h"
#include "blk_snap.h"
#include "chunk.h"
#include "diff_area.h"
#include "diff_storage.h"

#ifndef HAVE_BDEV_NR_SECTORS
static inline
sector_t bdev_nr_sectors(struct block_device *bdev)
{
	return i_size_read(bdev->bd_inode) >> 9;
};
#endif

/*static inline
unsigned long chunk_pages(struct diff_area *diff_area)
{
	return (1UL << (diff_area->chunk_shift - PAGE_SHIFT));
};*/
static inline
unsigned long chunk_number(struct diff_area *diff_area, sector_t sector)
{
	return (unsigned long)(sector >> (diff_area->chunk_shift - SECTOR_SHIFT));
};

static inline
void recalculate_last_chunk_size(struct chunk *chunk)
{
	struct diff_area *diff_area = chunk->diff_area;
	sector_t capacity;

	capacity = bdev_nr_sectors(diff_area->orig_bdev);
	chunk->sector_count = capacity - round_down(capacity, chunk->sector_count);
}

static inline
unsigned long long count_by_shift(sector_t capacity, unsigned long long shift)
{
	return round_up(capacity, 1ull << (shift - SECTOR_SHIFT)) >> (shift - SECTOR_SHIFT);
}

static
void diff_area_calculate_chunk_size(struct diff_area *diff_area)
{
	unsigned long long shift = chunk_minimum_shift;
	unsigned long long count;
	sector_t capacity;
	sector_t min_io_sect;

	min_io_sect = (sector_t)(bdev_io_min(diff_area->orig_bdev) >> SECTOR_SHIFT);
	capacity = bdev_nr_sectors(diff_area->orig_bdev);

	count = count_by_shift(capacity, shift);
	while ((count > chunk_maximum_count) || (diff_area_chunk_sectors(diff_area) < min_io_sect)) {
		shift = shift << 1;
		count = count_by_shift(capacity, shift);
	}

	diff_area->chunk_shift = shift;
	diff_area->chunk_count = count;
}

static inline
struct chunk *get_chunk_from_storing_list(struct diff_area *diff_area)
{
	struct chunk *chunk;

	spin_lock(&diff_area->storing_chunks_lock);
	chunk = list_first_entry_or_null(&diff_area->storing_chunks, struct chunk, link);
	if (chunk)
		list_del(&chunk->link);
	spin_unlock(&diff_area->storing_chunks_lock);

	return chunk;
}

static inline
struct chunk *get_chunk_from_cache(struct diff_area *diff_area)
{
	struct chunk *chunk;

	spin_lock(&diff_area->caching_chunks_lock);
	chunk = list_first_entry_or_null(&diff_area->caching_chunks, struct chunk, link);
	if (chunk)
		list_del(&chunk->link);
	spin_unlock(&diff_area->caching_chunks_lock);

	return chunk;
}

void diff_area_free(struct kref *kref)
{
	unsigned long inx;
	struct chunk *chunk;
	struct diff_area *diff_area = container_of(kref, struct diff_area, kref);

	flush_work(&diff_area->storing_chunks_work);
	flush_work(&diff_area->caching_chunks_work);

	xa_for_each(&diff_area->chunk_map, inx, chunk)
		chunk_free(chunk);//chunk_put(chunk);
	xa_destroy(&diff_area->chunk_map);

	if (diff_area->io_client) {
		dm_io_client_destroy(diff_area->io_client);
		diff_area->io_client = NULL;
	}

	if (diff_area->orig_bdev) {
		blkdev_put(diff_area->orig_bdev, FMODE_READ | FMODE_WRITE);
		diff_area->orig_bdev = NULL;
	}
}

static inline
void schedule_cache(struct chunk *chunk)
{
	struct diff_area *diff_area = chunk->diff_area;

	spin_lock(&diff_area->caching_chunks_lock);
	list_add_tail(&chunk->link, &diff_area->caching_chunks);
	atomic_inc(&diff_area->caching_chunks_count);
	spin_unlock(&diff_area->caching_chunks_lock);

	up_read(&chunk->lock);

	/* Initiate the cache clearing process. */
	queue_work(system_wq, &diff_area->caching_chunks_work);
}

static inline
void io_failed(struct chunk *chunk, int error)
{
	chunk_state_set(chunk, CHUNK_ST_FAILED);
	diff_area_set_corrupted(chunk->diff_area, error);
	pr_err("Failed to write chunk %lu\n", chunk->number);
};

static
void notify_store_fn(unsigned long error, void *context)
{
	struct chunk *chunk = (struct chunk *)context;

	if (error) {
		io_failed(chunk, error);
		up_read(&chunk->lock);
		return;
	}
	chunk_state_set(chunk, CHUNK_ST_STORE_READY);

	pr_err("Chunk 0x%lu was stored\n", chunk->number);

	schedule_cache(chunk);
}

static
void diff_area_storing_chunks_work(struct work_struct *work)
{
	int ret;
	struct diff_area *diff_area = container_of(work, struct diff_area, storing_chunks_work);
	struct chunk *chunk;

	while ((chunk = get_chunk_from_storing_list(diff_area))) {
		if (!chunk->diff_store) {
			struct diff_store *diff_store;

			diff_store = diff_storage_get_store(
					diff_area->diff_storage,
					diff_area_chunk_sectors(diff_area));
			if (unlikely(IS_ERR(diff_store))) {
				io_failed(chunk, PTR_ERR(diff_store));
				up_read(&chunk->lock);
				return;
			}

			chunk->diff_store = diff_store;
		}

		ret = chunk_async_store_diff(chunk, notify_store_fn);
		if (ret)
			io_failed(chunk, ret);
		up_read(&chunk->lock);
	}

}

static
void diff_area_caching_chunks_work(struct work_struct *work)
{
	struct diff_area *diff_area = container_of(work, struct diff_area, caching_chunks_work);
	struct chunk *chunk = NULL;


	while (atomic_read(&diff_area->caching_chunks_count) >
	       chunk_maximum_in_cache) {

		chunk = get_chunk_from_cache(diff_area);
		if (!chunk)
			break;

		if (!down_write_trylock(&chunk->lock)) {
			/**
			 * If a used block is detected, it is moved to the end
			 * of the queue.
			 */
			spin_lock(&diff_area->caching_chunks_lock);
			list_add_tail(&chunk->link, &diff_area->caching_chunks);
			spin_unlock(&diff_area->caching_chunks_lock);

			continue;
		}

		chunk_free_buffer(chunk);
		atomic_dec(&diff_area->caching_chunks_count);
		up_write(&chunk->lock);
	}
}

struct diff_area *diff_area_new(dev_t dev_id, struct diff_storage *diff_storage)
{
	int ret = 0;
	struct diff_area *diff_area = NULL;
	struct dm_io_client *cli;
	struct block_device *bdev;
	unsigned long number;
	struct chunk *chunk;

	pr_info("Open device [%u:%u]\n", MAJOR(dev_id), MINOR(dev_id));

	bdev = blkdev_get_by_dev(dev_id, FMODE_READ | FMODE_WRITE, NULL);
	if (IS_ERR(bdev)) {
		pr_err("Failed to open device. errno=%ld\n", PTR_ERR(bdev));
		return ERR_PTR(PTR_ERR(bdev));
	}

	cli = dm_io_client_create();
	if (IS_ERR(cli)) {
		blkdev_put(bdev, FMODE_READ | FMODE_WRITE);
		return ERR_PTR(PTR_ERR(cli));
	}

	diff_area = kzalloc(sizeof(struct diff_area), GFP_KERNEL);
	if (!diff_area) {
		dm_io_client_destroy(cli);
		blkdev_put(bdev, FMODE_READ | FMODE_WRITE);
		return ERR_PTR(-ENOMEM);
	}

	diff_area->orig_bdev = bdev;
	diff_area->io_client = cli;
	diff_area->diff_storage = diff_storage;

	diff_area_calculate_chunk_size(diff_area);
	pr_info("Chunk size %llu in bytes\n", 1ULL << diff_area->chunk_shift);
	pr_info("chunk count %lu\n", diff_area->chunk_count);

	kref_init(&diff_area->kref);
	xa_init(&diff_area->chunk_map);

	INIT_LIST_HEAD(&diff_area->storing_chunks);
	spin_lock_init(&diff_area->storing_chunks_lock);
	INIT_WORK(&diff_area->storing_chunks_work, diff_area_storing_chunks_work);

	INIT_LIST_HEAD(&diff_area->caching_chunks);
	spin_lock_init(&diff_area->caching_chunks_lock);
	INIT_WORK(&diff_area->caching_chunks_work, diff_area_caching_chunks_work);

	/**
	 * Allocating all chunks in advance allows not to do this in
	 * the process of filtering bio.
	 * In addition, the chunk structure has an rw semaphore that allows
	 * to lock data of a single chunk.
	 * Different threads can read, write, or dump their data to diff storage
	 * independently of each other, provided that different chunks are used.
	 */
	for (number = 0; number < diff_area->chunk_count; number++) {
		chunk = chunk_alloc(diff_area, number);
		if (!chunk) {
			pr_err("Failed allocate chunk");
			ret = -ENOMEM;
			break;
		}
		chunk->sector_count = diff_area_chunk_sectors(diff_area);

		ret = xa_insert(&diff_area->chunk_map, number, chunk, GFP_KERNEL);
		if (ret) {
			pr_err("Failed insert chunk to chunk map");
			chunk_free(chunk);//chunk_put(chunk);
			break;
		}
	}
	if (ret) {
		diff_area_put(diff_area);
		return ERR_PTR(ret);
	}

	recalculate_last_chunk_size(chunk);

	atomic_set(&diff_area->corrupted_flag, 0);
	return diff_area;
}

static inline
void schedule_storing_diff(struct chunk *chunk)
{
	struct diff_area *diff_area = chunk->diff_area;

	spin_lock(&diff_area->storing_chunks_lock);
	list_add_tail(&chunk->link, &diff_area->storing_chunks);
	spin_unlock(&diff_area->storing_chunks_lock);

	downgrade_write(&chunk->lock);

	/**
	 * I believe that the priority of the COW algorithm completion process
	 * should be high so that the scheduler can preempt other threads to
	 * complete the write.
	 */
	queue_work(system_highpri_wq, &diff_area->storing_chunks_work);
}

static
void notify_load_fn(unsigned long error, void *context)
{
	struct chunk *chunk = (struct chunk *)context;

	if (error) {
		io_failed(chunk, error);
		up_write(&chunk->lock);
		return;
	}
	chunk_state_set(chunk, CHUNK_ST_DIRTY);
	chunk_state_set(chunk, CHUNK_ST_BUFFER_READY);

	pr_debug("Chunk 0x%lu was read\n", chunk->number);

	schedule_storing_diff(chunk);
}

int diff_area_copy(struct diff_area *diff_area, sector_t sector, sector_t count,
                   bool is_nowait)
{
	int ret;
	sector_t offset;
	struct chunk *chunk;
	sector_t area_sect_first;
	sector_t area_sect_end;

	//pr_err("bio sector %lld count %lld\n", sector, count);

	area_sect_first = round_down(sector, diff_area_chunk_sectors(diff_area));
	area_sect_end = round_up(sector + count, diff_area_chunk_sectors(diff_area));

	//pr_err("copy sectors form [%lld to %lld)\n", area_sect_first, area_sect_end);

	for (offset = area_sect_first;
	     offset < area_sect_end;
	     offset += diff_area_chunk_sectors(diff_area)) {
		unsigned long number = chunk_number(diff_area, offset);

		chunk = xa_load(&diff_area->chunk_map, number);
		if (!chunk)
			return -EINVAL;

		if (chunk_state_check(chunk, CHUNK_ST_DIRTY))
			continue;

		if (is_nowait) {
			if (!down_write_trylock(&chunk->lock))
				return -EAGAIN;
		}
		else
			down_write(&chunk->lock);

		if (chunk_state_check(chunk, CHUNK_ST_DIRTY)) {
			up_write(&chunk->lock);
			continue;
		}

		if (chunk_state_check(chunk, CHUNK_ST_BUFFER_READY)) {
			/**
			 * The chunk has already been read from original device
			 * into memory, and need to store it in diff storage.
			 */
			chunk_state_set(chunk, CHUNK_ST_DIRTY);
			schedule_storing_diff(chunk);
			continue;
		}

		if (is_nowait) {
			ret = chunk_allocate_buffer(chunk, GFP_NOIO | GFP_NOWAIT);
			if (ret) {
				up_write(&chunk->lock);
				return -EAGAIN;
			}
		} else {
			ret = chunk_allocate_buffer(chunk, GFP_NOIO);
			if (ret) {
				up_write(&chunk->lock);
				return ret;
			}
		}

		ret = chunk_asunc_load_orig(chunk, notify_load_fn);
		if (ret) {
			up_write(&chunk->lock);
			return ret;
		}
	}

	return 0;
}

void diff_area_image_ctx_done(struct diff_area_image_ctx *io_ctx)
{
	if (!io_ctx->chunk)
		return;

	if (io_ctx->is_write)
		schedule_storing_diff(io_ctx->chunk);
	else
		schedule_cache(io_ctx->chunk);
}

static
struct chunk* diff_area_image_context_get_chunk(
	struct diff_area_image_ctx *io_ctx,
	sector_t sector)
{
	struct chunk* chunk;
	struct diff_area *diff_area = io_ctx->diff_area;
	unsigned long new_chunk_number = chunk_number(diff_area, sector);

	if (io_ctx->chunk) {
		if (io_ctx->chunk->number == new_chunk_number)
			return io_ctx->chunk;

		/*
		 * If the sector falls into a new chunk, then we release
		 * the old chunk.
		 */
		if (io_ctx->chunk) {
			if (io_ctx->is_write)
				schedule_storing_diff(chunk);
			else
				schedule_cache(chunk);

			io_ctx->chunk = NULL;
		}
	}

	/* Take a next chunk. */
	chunk = xa_load(&diff_area->chunk_map, new_chunk_number);
	if (unlikely(!chunk))
		return ERR_PTR(-EINVAL);

	if (io_ctx->is_write)
		down_write(&chunk->lock);
	else
		down_read(&chunk->lock);

	if (unlikely(chunk_state_check(chunk, CHUNK_ST_FAILED))) {
		up_write(&chunk->lock);
		return ERR_PTR(-EIO);
	}

	io_ctx->chunk = chunk;

	/* Up chunk in cache */
	spin_lock(&diff_area->caching_chunks_lock);
	list_del(&chunk->link);
	list_add_tail(&chunk->link, &diff_area->caching_chunks);
	spin_unlock(&diff_area->caching_chunks_lock);

	/*
	 * If there is already data in the buffer, then nothing needs to be load.
	 * A chunk should be read from diff_storage
	 */
	if (!chunk_state_check(chunk, CHUNK_ST_BUFFER_READY)) {
		if (chunk_state_check(chunk, CHUNK_ST_STORE_READY))
			chunk_load_diff(chunk);
		else
			chunk_load_orig(chunk);
	}

	return chunk;
}

static inline
sector_t diff_area_chunk_start(struct diff_area *diff_area, struct chunk *chunk)
{
	return (sector_t)(chunk->number) << diff_area->chunk_shift;
}

/**
 * diff_area_image_io - implements copying data from chunk to bio_vec when
 * reading or from bio_tec to chunk when writing.
 */
blk_status_t diff_area_image_io(struct diff_area_image_ctx *io_ctx,
				const struct bio_vec *bvec, sector_t *pos)
{
	unsigned int bv_len = bvec->bv_len;
	struct iov_iter iter;

	iov_iter_bvec(&iter, io_ctx->is_write ? WRITE : READ, bvec, 1, bv_len);

	while (bv_len) {
		struct diff_buffer_iter diff_buffer_iter;
		struct chunk *chunk;

		chunk = diff_area_image_context_get_chunk(io_ctx, *pos);
		if (IS_ERR(chunk))
			return BLK_STS_IOERR;

		while(bv_len && diff_buffer_iter_get(chunk->diff_buffer, *pos, &diff_buffer_iter)) {
			ssize_t sz;

			if (io_ctx->is_write)
				sz = copy_page_from_iter(diff_buffer_iter.page,
			        	                 diff_buffer_iter.offset,
			                	         diff_buffer_iter.bytes,
			                        	 &iter);
			else
				sz = copy_page_to_iter(diff_buffer_iter.page,
			        	               diff_buffer_iter.offset,
			                	       diff_buffer_iter.bytes,
				                       &iter);
			if (!sz)
				return BLK_STS_IOERR;

			*pos += (sz >> SECTOR_SHIFT);
			bv_len -= sz;
		}
	}

	return BLK_STS_OK;
}

void diff_area_set_corrupted(struct diff_area *diff_area, int err_code)
{
	struct blk_snap_event_corrupted data;

	if (atomic_inc_return(&diff_area->corrupted_flag) != 1)
		return;

	data.orig_dev_id.mj = MAJOR(diff_area->orig_bdev->bd_dev);
	data.orig_dev_id.mn = MINOR(diff_area->orig_bdev->bd_dev);
	data.errno = abs(err_code);

	event_gen(&diff_area->diff_storage->event_queue, GFP_NOIO,
		BLK_SNAP_EVENT_CORRUPTED,
		&data, sizeof(data));

	pr_err("Set snapshot device is corrupted for [%u:%u] with error code %d.\n",
	       data.orig_dev_id.mj, data.orig_dev_id.mn, abs(data.errno));
}
