// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-diff-area"
#include "common.h"
#include "chunk.h"
#include "diff_area.h"
#include "diff_storage.h"

struct diff_area {
	struct kref kref;
	struct block_device *bdev;
	struct dm_io_client *io_client;
	/**
	 * chunk_size_shift - power of 2 for chunk size
	 * It's allow to set various chunk size for huge and small block device.
	 */
	unsigned long long chunk_size_shift;
	/**
	 * chunk_count - count of chunks
	 * the number of chunks into which the block device is divided.
	 */
	unsigned long chunk_count;
	/**
	 * chunk_map - a map of chunk
	 * xarray can be too little on x32 systems. This creates a limit
	 * in the size of supported disks to 256 TB with a chunk size
	 * of 65536 bytes. In addition, such a large array will take up too
	 * much space in memory.
	 * Therefore, the size of the chunk should be selected so that
	 * the size of the map is not too large, and the index does not
	 * exceed 32 bits.
	 */
	struct xarray chunk_map;
	/**
	 * in_process_chunks - a list of chunks that in COW process
	 */
	struct list_head in_process_chunks;
	//rwlock_t chunk_fifo_lock;
	//size_t chunk_fifo_cnt;
	/**
	 * Same diff_area can use one diff_storage for storing his chunks
	 * 
	 */
	struct diff_storage *diff_storage;

	/**
	 * Reading data from the original block device is performed in the
	 * context of the thread in which the filtering is performed.
	 * But storing data to diff storage is performed in workqueue.
	 * The chunks that need to be stored in diff storage are accumitale
	 * into the diff_store_changs list.
	 */
	struct list_head diff_store_changs;
	spinlock_t diff_store_changs_lock;

	int error;
	bool is_corrupted;
};

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

	if (unlikely(number > CONFIG_BLK_SNAP_MAXIMUM_CHUNK_COUNT)) {
		WARN("The number of the chunk has exceeded the acceptable limit");
		diff_area->is_corrupted = true;
	}
	
	return (unsigned long)number;
};

static inline void __recalculate_last_chunk_size(struct chunk *chunk)
{
	struct diff_area *diff_area = chunk->diff_area;
	sector_t capacity;

	capacity = part_nr_sects_read(diff_area->bdev->bd_part);
	chunk->sector_count = capacity - round_down(capacity, chunk->sector_count);
}

static void diff_area_calculate_chunk_size(struct diff_area *diff_area)
{
	unsigned long long shift = CONFIG_BLK_SNAP_CHUNK_MINIMUM_SHIFT;
	unsigned long long count;
	sector_t capacity;
	sector_t min_io_sect;

	min_io_sect = (sector_t)(bdev_io_min(diff_area->bdev) >> SECTOR_SHIFT);
	capacity = part_nr_sects_read(diff_area->bdev->bd_part);

	count = round_up(capacity, 1 << (shift - SECTOR_SHIFT)) >> (shift - SECTOR_SHIFT);
	while ((count > CONFIG_BLK_SNAP_MAXIMUM_CHUNK_COUNT) ||
		(chunk_sectors(shift) < min_io_sect)) {
		shift = shift << 1;
		count = round_up(capacity, 1 << (shift - SECTOR_SHIFT)) >> (shift - SECTOR_SHIFT);
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

	if (diff_area->bdev) {
		blkdev_put(diff_area->bdev);
		diff_area->bdev = NULL;
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

	diff_area->bdev = bdev;
	diff_area->io_client = cli;
	diff_area->diff_storage = diff_storage;

	diff_area_calculate_chunk_size(diff_area);
	pr_info("Chunk size %llu bytes\n", chunk_size(diff_area->chunk_size_shift));
	pr_info("chunk count %lu\n", diff_area->chunk_count);

	xa_init(&diff_area->chunk_map);
	INIT_LIST_HEAD(&diff_area->chunk_fifo);
	rwlock_init(&diff_area->chunk_fifo_lock);
	kref_init(&diff_area->refcount);

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
	chunk->diff_area->is_corrupted = true;
	chunk->diff_area->error = error;
	
	pr_err("Failed to write chunk %lu\n", chunk->number);
}

static void chunk_notify_write(unsigned long error, void *context)
{
	struct chunk *chunk = (struct chunk *)context;
	struct diff_area *diff_area = chunk->diff_area;

	if (error) {
		chunk_io_failed(chunk, error);
		goto out;
	}
	spin_lock(&chunk->lock);
	chunk_state_set(chunk, CHUNK_ST_STORE_READY);
	spin_unlock(&chunk->lock);

	pr_err("Chunk 0x%lu was wrote\n", chunk->number);
out:
	chunk_put(chunk);
}

static void __diff_area_chunk_write()
{
	struct chunk *chunk;
	unsigned long sync_error_bits;
	struct dm_io_region region;
	struct dm_io_request reguest;

	dm_io_region region = {
		.bdev = chunk->diff_store->bdev,
		.sector = chunk->diff_store->sector,
		.count = chunk->diff_store->count,
	};
	dm_io_request reguest = {
		.bi_op = REQ_OP_WRITE,
		.bi_op_flags = 0,
		.mem.type = DM_IO_PAGE_LIST,
		.mem.offset = 0,
		.mem.ptr.pl = chunk_first_page(chunk),
		.notify.fn = chunk_notify_write,
		.notify.context = chunk,
		.client = chunk->diff_area->io_client,
	};

	ret = dm_io(&reguest, 1, &region, &sync_error_bits);
	if (ret)
		chunk_io_failed(chunk, ret);
}

static void chunk_schedule_store(struct chunk *chunk)
{
	struct diff_area *diff_area = chunk->diff_area;

	spin_lock(&diff_area->diff_store_chungs_lock);
	list_add_tail(&chunk->link, &diff_area->diff_store_chungs);
	spin_lock(&diff_area->diff_store_chungs_unlock);


	//INIT_WORK(diff_area->work, __diff_area_chunk_write);
	//atomic_long_inc(&diff_area->work->data);
	/**
	 * I believe that the priority for the completion process of the COW
	 * algorithm should be high, so that threads writing to block devices
	 * are preempted by the scheduler.
	 */
	queue_work(system_highpri_wq, diff_area->work); 
	//queue_work(system_wq_hi, work);

	/*
	chunk->diff_store = diff_storage_get_store(diff_area->diff_storage, chunk_sectors(diff_area));
	if (!chunk->diff_store) {
		chunk_io_failed(chunk, -ENOMEM);
		goto out;
	}

	error = __diff_area_chunk_write(chunk);
	if (error)
		chunk_io_failed(chunk, error)
	return;*/
}

static void chunk_notify_read(unsigned long error, void *context)
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

	up_write(&chunk->lock);

	chunk_schedule_store(chunk);
}

static int __diff_area_chunk_read(struct chunk *chunk)
{
	unsigned long sync_error_bits;
	struct dm_io_region region = {
		.bdev = chunk->diff_area->bdev,
		.sector = (sector_t)chunk->number * chunk_sectors(chunk->diff_area),
		.count = chunk_sectors(chunk->diff_area),
	};
	struct dm_io_request reguest = {
		.bi_op = REQ_OP_READ,
		.bi_op_flags = 0,
		.mem.type = DM_IO_PAGE_LIST,
		.mem.offset = 0,
		.mem.ptr.pl = chunk_first_page(chunk),
		.notify.fn = chunk_notify_read,
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
	sector_t area_sect_first;
	sector_t area_sect_end;
	sector_t offset;
	struct chunk *chunk;
	LIST_HEAD(chunk_list);
	int chunk_alloc_count = 0;
	sector_t __chunk_sectors = chunk_sectors(diff_area->chunk_size_shift);

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

		ret = __diff_area_chunk_read(chunk);
		if (ret) {
			up_write(chunk);
			return ret;
		}
	}

	return 0;

fail_free_chunk_list:
	while((chunk = list_first_entry_or_null(&chunk_list, struct chunk, link))) {
		list_del(&chunk->link);
		chunk_put(chunk);
	}
	return ret;
}


blk_status_t diff_area_image_write(struct diff_area *diff_area,
				   struct page *page, unsigned int page_off,
				   sector_t sector, unsigned int len)
{
	unsigned long number = chunk_number(diff_area, sector);

	chunk = xa_load(&diff_area->chunk_map, number);
	if (!chunk) {
		/* Chunk should be read */

	}
}

blk_status_t diff_area_image_read(struct diff_area *diff_area,
				  struct page *page, unsigned int page_off,
				  sector_t sector, unsigned int len)
{

}
