// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-diff-area"
#include "common.h"
#include "chunk.h"
#include "diff_area.h"
#include "diff_storage.h"


static inline unsigned long chunk_size(struct diff_area *diff_area)
{
	return (1UL << diff_area->chunk_size_shift);
};
static inline sector_t chunk_sectors(struct diff_area *diff_area)
{
	return (sector_t)(1ULL << (diff_area->chunk_size_shift - SECTOR_SHIFT));
};
static inline unsigned long chunk_pages(struct diff_area *diff_area)
{
	return (1UL << (diff_area->chunk_size_shift - PAGE_SHIFT));
};
static inline unsigned long chunk_number(struct diff_area *diff_area, sector_t sector)
{
	sector_t number = (offset >> (diff_area->chunk_size_shift - SECTOR_SHIFT));
/*
	if (unlikely(number > CONFIG_BLK_SNAP_MAXIMUM_CHUNK_COUNT)) {
		WARN("The number of the chunk has exceeded the acceptable limit");
		diff_area->is_corrupted = true;
	}
*/
	return (unsigned long)number;
};

static inline void __recalculate_last_chunk_size(struct chunk *chunk)
{
	struct diff_area *diff_area = chunk->diff_area;
	sector_t capacity;

	capacity = bdev_nr_sectors(diff_area->orig_bdev);
	chunk->sector_count = capacity - round_down(capacity, chunk->sector_count);
}

static inline unsigned long long __count_by_shift(sector_t capacity, unsigned long long shift)
{
	return round_up(capacity, 1ull << (shift - SECTOR_SHIFT)) >> (shift - SECTOR_SHIFT);
}

static void diff_area_calculate_chunk_size(struct diff_area *diff_area)
{
	unsigned long long shift = CONFIG_BLK_SNAP_CHUNK_MINIMUM_SHIFT;
	unsigned long long count;
	sector_t capacity;
	sector_t min_io_sect;

	min_io_sect = (sector_t)(bdev_io_min(diff_area->orig_bdev) >> SECTOR_SHIFT);
	capacity = bdev_nr_sectors(diff_area->orig_bdev);

	count = __count_by_shift(capacity, shift);
	while ((count > CONFIG_BLK_SNAP_MAXIMUM_CHUNK_COUNT) || (chunk_sectors(shift) < min_io_sect)) {
		shift = shift << 1;
		count = __count_by_shift(capacity, shift);
	}

	diff_area->chunk_size_shift = shift;
	diff_area->chunk_count = count;
}

void diff_area_free(struct kref *kref)
{
	struct diff_area *diff_area = container_of(kref, struct diff_area, kref);

	write_lock(&diff_area->chunk_fifo_lock);
	while((chunk = list_first_entry_or_null(&diff_area->chunk_fifo, struct chunk, link))) {
		list_del(&chunk->link);
		chunk_put(chunk);
	}
	write_unlock(&diff_area->chunk_fifo_lock);

	xa_for_each(&diff_area->chunk_map, inx, chunk)
		chunk_put(chunk);
	xa_destroy(&diff_area->chunk_map);

	if (diff_area->io_client) {
		dm_io_client_destroy(diff_area->io_client);
		diff_area->io_client = NULL;
	}

	if (diff_area->orig_bdev) {
		blkdev_put(diff_area->orig_bdev);
		diff_area->orig_bdev = NULL;
	}
}

static void __diff_area_chunk_store(struct chunk *chunk);
static void diff_area_storing_chunks_work(struct work_struct *work)
{
	struct diff_area *diff_area = container_of(work, struct diff_area, work);
	struct chunk *chunk = NULL;

repeat:
	spin_lock(&diff_area->storing_chunks_lock);
	chunk = list_first_entry_or_null(&diff_area->storing_chunks, struct chunk, link);
	if (!chunk) {
		spin_unlock(&diff_area->storing_chunks_lock);
		return;
	}
	list_del(&chunk->list);
	spin_unlock(&diff_area->storing_chunks_lock);

	chunk->diff_store = diff_storage_get_store(diff_area->diff_storage,
	                                           chunk_sectors(diff_area->chunk_size_shift));
	if (unlikely(IS_ERR(chunk->diff_store))) {
		diff_area_set_corrupted(diff_area);
		return;
	}

	__diff_area_chunk_store(chunk);
	goto repeat;

}

static void diff_area_caching_chunks_work(struct work_struct *work)
{
	struct diff_area *diff_area = container_of(work, struct diff_area, work);
	struct chunk *chunk = NULL;


	while (atomic_read(&diff_area->caching_chunks_count) >
	       CONFIG_BLK_SNAP_MAXIMUM_CHUNK_IN_CACHE) {

		spin_lock(&diff_area->caching_chunks_lock);
		chunk = list_first_entry_or_null(&diff_area->caching_chunks, struct chunk, link);
		if (!chunk) {
			spin_unlock(&diff_area->caching_chunks_lock);
			break;
		}
		list_del(&chunk->list);
		spin_unlock(&diff_area->caching_chunks_lock);

		if (!down_write_trylock(&chunk->lock)) {
			/**
			 * If a used block is detected, it is moved to the end
			 * of the queue.
			 */
			spin_lock(&diff_area->caching_chunks_lock);
			list_del(&chunk->list);
			list_add_tail(&chunk->list, &diff_area->caching_chunks);
			spin_unlock(&diff_area->caching_chunks_lock);

			continue;
		}

		diff_buffer_put(&chunk->diff_buffer);
		chunk->diff_buffer = NULL;
		chunk_state_unset(chunk, CHUNK_ST_BUFFER_READY);

		up_write(&chunk->lock);
		atomic_dec(&diff_area->caching_chunks_count);
	}
}

struct diff_area *diff_area_new(dev_t dev_id, struct diff_storage *diff_storage, struct event_queue *event_queue)
{
	int ret = 0;
	struct diff_area *diff_area = NULL;
	struct dm_io_client *cli;
	struct block_device *bdev;
	unsigned long number;
	struct chunk *chunk;


	pr_info("Open device [%d:%d]\n", MAJOR(dev_id), MINOR(dev_id));

	bdev = blkdev_get_by_dev(dev_id, 0, NULL);
	if (IS_ERR(bdev)) {
		pr_err("Failed to open device. errno=%ld\n", PTR_ERR(bdev));
		return PTR_ERR(bdev);
	}

	cli = dm_io_client_create();
	if (IS_ERR(cli)) {
		blkdev_put(bdev);
		return PTR_ERR(cli);
	}

	diff_area = kzalloc(sizeof(struct diff_area), GFP_KERNEL);
	if (!diff_area) {
		dm_io_client_destroy(cli);
		blkdev_put(bdev);
		return ERR_PTR(-ENOMEM);
	}

	diff_area->orig_bdev = bdev;
	diff_area->io_client = cli;
	diff_area->diff_storage = diff_storage;
	diff_area->event_queue = event_queue;

	diff_area_calculate_chunk_size(diff_area);
	pr_info("Chunk size %llu bytes\n", chunk_size(diff_area->chunk_size_shift));
	pr_info("chunk count %lu\n", diff_area->chunk_count);

	kref_init(&diff_area->refcount);
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
		chunk->sector_count = chunk_sectors(diff_area->chunk_size_shift);

		ret = xa_insert(&diff_area->chunk_map, number, chunk, GFP_KERNEL);
		if (ret) {
			pr_err("Failed insert chunk to chunk map");
			chunk_put(chunk);
			break;
		}
	}
	if (ret) {
		diff_area_put(diff_area);
		return ERR_PTR(ret);		
	}

	__recalculate_last_chunk_size(chunk);

	atomic_set(&diff_area->corrupted_flag, 0);
	return diff_area;
}

static inline int chunk_allocate_buffer(struct chunk *chunk, gfp_t gfp_mask)
{
	struct diff_buffer *buf;
	size_t page_count;

	page_count = round_up(chunk->sector_count, (1 << (PAGE_SHIFT - SECTOR_SHIFT)));
	buf = diff_buffer_new(page_count, gfp_mask);
	if (!buf) {
		pr_err("Failed allocate memory buffer for chunk");
		up_write(chunk);
		return -ENOMEM;
	}
	chunk->diff_buffer = buf;	
}

static inline void chunk_io_failed(struct chunk *chunk, int error)
{
	chunk_state_set(chunk, CHUNK_ST_FAILED);
	diff_area_set_corrupted(chunk->diff_area, error);
	pr_err("Failed to write chunk %lu\n", chunk->number);
}

static void chunk_notify_store(unsigned long error, void *context)
{
	struct chunk *chunk = (struct chunk *)context;
	struct diff_area *diff_area = chunk->diff_area;

	if (error) {
		chunk_io_failed(chunk, error);
		up_read(&chunk->lock);
		return;
	}

	chunk_state_set(chunk, CHUNK_ST_STORE_READY);

	spin_lock(&diff_area->caching_chunks_lock);
	list_add_tail(&chunk->link, &diff_area->caching_chunks);
	atomic_inc(&diff_area->caching_chunks_count);
	spin_unlock(&diff_area->caching_chunks_lock);

	up_read(&chunk->lock);

	pr_err("Chunk 0x%lu was wrote\n", chunk->number);

	/**
	 * Initiate the cache clearing process.
	 */
	queue_work(system_wq, diff_area->caching_chunks_work); 
}

static void __diff_area_chunk_store(struct chunk *chunk)
{
	unsigned long sync_error_bits;
	struct dm_io_region region;
	struct dm_io_request reguest;

	dm_io_region region = {
		.bdev = chunk->diff_store->bdev,
		.sector = chunk->diff_store->sector,
		.count = chunk->sector_count,
	};
	dm_io_request reguest = {
		.bi_op = REQ_OP_WRITE,
		.bi_op_flags = 0,
		.mem.type = DM_IO_PAGE_LIST,
		.mem.offset = 0,
		.mem.ptr.pl = chunk_first_page(chunk),
		.notify.fn = chunk_notify_store,
		.notify.context = chunk,
		.client = chunk->diff_area->io_client,
	};

	ret = dm_io(&reguest, 1, &region, &sync_error_bits);
	if (ret) {
		chunk_io_failed(chunk, ret);
		up_read(&chunk->lock);
	}
}

static void chunk_notify_load(unsigned long error, void *context)
{
	struct chunk *chunk = (struct chunk *)context;
	struct diff_area *diff_area = chunk->diff_area;

	if (error) {
		chunk_io_failed(chunk, error);
		up_write(&chunk->lock);
		return
	}
	chunk_state_set(chunk, CHUNK_ST_DIRTY);
	chunk_state_set(chunk, CHUNK_ST_BUFFER_READY);

	pr_debug("Chunk 0x%lu was read\n", chunk->number);

	spin_lock(&diff_area->storing_chunks_lock);
	list_add_tail(&chunk->link, &diff_area->storing_chunks);
	spin_unlock(&diff_area->storing_chunks_lock);

	downgrade_write(&chunk->lock);

	/**
	 * I believe that the priority of the COW algorithm completion process
	 * should be high so that the scheduler can preempt other threads to
	 * complete the write.
	 */
	queue_work(system_highpri_wq, diff_area->storing_chunks_work); 
	//queue_work(system_wq, storing_chunks_work);

}

static int __diff_area_chunk_load(struct chunk *chunk)
{
	unsigned long sync_error_bits;
	struct dm_io_region region = {
		.bdev = chunk->diff_area->orig_bdev,
		.sector = (sector_t)chunk->number * chunk_sectors(chunk->diff_area),
		.count = chunk->sector_count,
	};
	struct dm_io_request reguest = {
		.bi_op = REQ_OP_READ,
		.bi_op_flags = 0,
		.mem.type = DM_IO_PAGE_LIST,
		.mem.offset = 0,
		.mem.ptr.pl = chunk_first_page(chunk),
		.notify.fn = chunk_notify_load,
		.notify.context = chunk,
		.client = chunk->diff_area->io_client,
	};

	chunk_get(chunk);
	ret = dm_io(&reguest, 1, &region, &sync_error_bits);
	if (ret)
		chunk_put(chunk);
	return ret;
}


int diff_area_copy(struct diff_area *diff_area, sector_t sector, sector_t count
                   bool is_nowait)
{
	int ret;
	sector_t offset;
	struct chunk *chunk;
	sector_t area_sect_first;
	sector_t area_sect_end;

	//pr_err("bio sector %lld count %lld\n", sector, count);

	area_sect_first = round_down(sector, __chunk_sectors);
	area_sect_end = round_up(sector + count, __chunk_sectors);

	//pr_err("copy sectors form [%lld to %lld)\n", area_sect_first, area_sect_end);

	for (offset = area_sect_first; offset < area_sect_end; offset += __chunk_sectors) {
		unsigned long number = chunk_number(diff_area, offset);

		chunk = xa_load(&diff_area->chunk_map, number);
		if (!chunk)
			return -EINVAL;

		if (chunk_state_check(CHUNK_ST_DIRTY))
			continue;

		if (is_nowait)
			if (!down_write_trylock(&chunk->lock))
				return -EAGAIN;
		else
			down_write(&chunk->lock);

		if (chunk_state_check(CHUNK_ST_DIRTY)) {
			up_write(chunk);
			continue;
		}

		if (chunk_state_check(CHUNK_ST_BUFFER_READY)) {
			/**
			 * The chunk has already been read from original device
			 * into memory, and need to store it in diff storage.
			 */
			chunk_state_set(chunk, CHUNK_ST_DIRTY);

			spin_lock(&diff_area->storing_chunks_lock);
			list_add_tail(&chunk->link, &diff_area->storing_chunks);
			spin_unlock(&diff_area->storing_chunks_lock);

			downgrade_write(&chunk->lock);

			queue_work(system_highpri_wq, diff_area->storing_chunks_work); 
			continue;
		}

		if (is_nowait) {
			ret = chunk_allocate_buffer(chunk, GFP_NOIO | GFP_NOWAIT);
			if (ret) {
				up_write(chunk);
				return -EAGAIN;		
			}
		} else {
			ret = chunk_allocate_buffer(chunk, GFP_NOIO)
			if (ret) {
				up_write(chunk);
				return ret;
			}
		}

		ret = __diff_area_chunk_load(chunk);
		if (ret) {
			up_write(chunk);
			return ret;
		}
	}

	return 0;
}

void diff_area_image_context_init(struct diff_area_image_context *image_ctx,
                                  void *disks_private_data)
{
	image_ctx->number = 0;
	image_ctx->chunk = NULL;
	image_ctx->diff_area = disk->private_data;
	diff_area_get(image_ctx->diff_area);
}

void diff_area_image_context_done(struct diff_area_image_context *image_ctx)
{
	if (image_ctx->chunk)
		up_write(&chunk->lock);
	diff_area_put(diff_area);
}

static int diff_area_image_context_warmup(struct diff_area_image_context *image_ctx,
                                          sector_t sector)
{
	struct diff_area *diff_area = image_ctx->diff_area;

	if (image_ctx->number != chunk_number(diff_area, sector) ) {
		/**
		 * If the sector falls into a new chunk, then we release
		 * the old chunk.
		 */
		if (image_ctx->chunk) {
			up_write(&image_ctx->chunk->lock);
			image_ctx->chunk = NULL;
		}

	}
	if (!image_ctx->chunk) {
		/**
		 *  And take a new one.
		 */
		image_ctx->number = chunk_number(diff_area, sector);

		image_ctx->chunk = xa_load(&diff_area->chunk_map, image_ctx->number);
		if (unlikely(!image_ctx->chunk))
			return -EINVAL;

		down_write(&image_ctx->chunk->lock);

		if (chunk_state_check(chunk, CHUNK_ST_BUFFER_READY))

	}
}
blk_status_t diff_area_image_write(struct diff_area *diff_area,
				   struct page *page, unsigned int page_off,
				   sector_t sector, unsigned int len)
{
	diff_area_image_context_warmup()

}

blk_status_t diff_area_image_read(struct diff_area *diff_area,
				  struct page *page, unsigned int page_off,
				  sector_t sector, unsigned int len)
{

}

/*
bool diff_area_is_corrupted(struct diff_area *diff_area)
{
	if (unlikely(diff_area->is_corrupted)) {
		if (unlikely(atomic_inc_return(&diff_area->req_failed_cnt) == 1))
			pr_err("Snapshot device is corrupted for [%d:%d]\n",
				MAJOR(diff_area->orig_dev->bd_dev),
				MINOR(diff_area->orig_dev->bd_dev));

		return true;
	}

	return false;
}
*/

void diff_area_set_corrupted(struct diff_area *diff_area, int err_code)
{
	if (atomic_inc_return(&diff_area->corrupted_flag) == 1) {
		struct blk_snap_event_overflow data = {
			.orig_dev_id = diff_area->orig_bdev->bd_dev;
			.errno = abs(err_code);
		};

		event_gen(&diff_storage->event_queue, GFP_NOIO
			BLK_SNAP_EVENT_CORRUPTED,
			data, sizeof(data));

		pr_err("Set snapshot device is corrupted for [%d:%d] with error code %d.\n",
		       MAJOR(data.orig_dev_id),
		       MINOR(data.orig_dev_id),
		       data.errno);
	}
}
/*
int diff_area_errno(dev_t dev_id, int *p_err_code)
{
	struct diff_area *diff_area;

	diff_area = snapstore_device_find_by_dev_id(dev_id);
	if (!diff_area)
		return -ENODATA;

	*p_err_code = snapstore_device->err_code;
	return SUCCESS;
}
*/
