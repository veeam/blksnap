// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-deferred"
#include "common.h"

#include "blk_deferred.h"
#include "blk_util.h"
#include "snapstore.h"
#include "params.h"

struct bio_set blk_deferred_bioset = { 0 };

struct dio_bio_complete {
	struct blk_deferred_request *dio_req;
	sector_t bio_sect_len;
};

struct dio_deadlocked_list {
	struct list_head link;

	struct blk_deferred_request *dio_req;
};

LIST_HEAD(dio_deadlocked_list);
DEFINE_RWLOCK(dio_deadlocked_list_lock);

atomic64_t dio_alloc_count = ATOMIC64_INIT(0);
atomic64_t dio_free_count = ATOMIC64_INIT(0);

void blk_deferred_done(void)
{
	struct dio_deadlocked_list *dio_locked;

	do {
		dio_locked = NULL;

		write_lock(&dio_deadlocked_list_lock);
		if (!list_empty(&dio_deadlocked_list)) {
			dio_locked = list_entry(dio_deadlocked_list.next,
						struct dio_deadlocked_list, link);

			list_del(&dio_locked->link);
		}
		write_unlock(&dio_deadlocked_list_lock);

		if (dio_locked) {
			if (dio_locked->dio_req->sect_len ==
			    atomic64_read(&dio_locked->dio_req->sect_processed))
				blk_deferred_request_free(dio_locked->dio_req);
			else
				pr_err("Locked defer IO is still in memory\n");

			kfree(dio_locked);
		}
	} while (dio_locked);
}

void blk_deferred_request_deadlocked(struct blk_deferred_request *dio_req)
{
	struct dio_deadlocked_list *dio_locked =
		kzalloc(sizeof(struct dio_deadlocked_list), GFP_KERNEL);

	dio_locked->dio_req = dio_req;

	write_lock(&dio_deadlocked_list_lock);
	list_add_tail(&dio_locked->link, &dio_deadlocked_list);
	write_unlock(&dio_deadlocked_list_lock);

	pr_warn("Deadlock with defer IO\n");
}

void blk_deferred_free(struct blk_deferred_io *dio)
{
	size_t inx = 0;

	if (dio->page_array != NULL) {
		while (dio->page_array[inx] != NULL) {
			__free_page(dio->page_array[inx]);
			dio->page_array[inx] = NULL;

			++inx;
		}

		kfree(dio->page_array);
		dio->page_array = NULL;
	}
	kfree(dio);
}

struct blk_deferred_io *blk_deferred_alloc(unsigned long block_index,
					   union blk_descr_unify blk_descr)
{
	size_t inx;
	size_t page_count;
	struct blk_deferred_io *dio = kmalloc(sizeof(struct blk_deferred_io), GFP_NOIO);

	if (dio == NULL)
		return NULL;

	INIT_LIST_HEAD(&dio->link);

	dio->blk_descr = blk_descr;
	dio->blk_index = block_index;

	dio->sect.ofs = block_index << snapstore_block_shift();
	dio->sect.cnt = snapstore_block_size();

	page_count = snapstore_block_size() / (PAGE_SIZE / SECTOR_SIZE);
	/*
	 * empty pointer on the end
	 */
	dio->page_array = kzalloc((page_count + 1) * sizeof(struct page *), GFP_NOIO);
	if (dio->page_array == NULL) {
		blk_deferred_free(dio);
		return NULL;
	}

	for (inx = 0; inx < page_count; inx++) {
		dio->page_array[inx] = alloc_page(GFP_NOIO);
		if (dio->page_array[inx] == NULL) {
			pr_err("Failed to allocate page\n");
			blk_deferred_free(dio);
			return NULL;
		}
	}

	return dio;
}

int blk_deferred_bioset_create(void)
{
	return bioset_init(&blk_deferred_bioset, 64, sizeof(struct dio_bio_complete),
			   BIOSET_NEED_BVECS | BIOSET_NEED_RESCUER);
}

void blk_deferred_bioset_free(void)
{
	bioset_exit(&blk_deferred_bioset);
}

static struct bio *_blk_deferred_bio_alloc(int nr_iovecs)
{
	struct bio *new_bio = bio_alloc_bioset(GFP_NOIO, nr_iovecs, &blk_deferred_bioset);

	if (new_bio == NULL)
		return NULL;

	new_bio->bi_end_io = blk_deferred_bio_endio;
	new_bio->bi_private = ((void *)new_bio) - sizeof(struct dio_bio_complete);

	return new_bio;
}

static void blk_deferred_complete(struct blk_deferred_request *dio_req, sector_t portion_sect_cnt,
				  int result)
{
	atomic64_add(portion_sect_cnt, &dio_req->sect_processed);

	if (dio_req->sect_len == atomic64_read(&dio_req->sect_processed))
		complete(&dio_req->complete);

	if (result != SUCCESS) {
		dio_req->result = result;
		pr_err("Failed to process defer IO request. errno=%d\n", result);
	}
}

void blk_deferred_bio_endio(struct bio *bio)
{
	int local_err;
	struct dio_bio_complete *complete_param = (struct dio_bio_complete *)bio->bi_private;

	if (complete_param == NULL) {
		//bio already complete
	} else {
		if (bio->bi_status != BLK_STS_OK)
			local_err = -EIO;
		else
			local_err = SUCCESS;

		blk_deferred_complete(complete_param->dio_req, complete_param->bio_sect_len,
				      local_err);
		bio->bi_private = NULL;
	}

	bio_put(bio);
}

static inline size_t _page_count_calculate(sector_t size_sector)
{
	size_t page_count = size_sector / (PAGE_SIZE / SECTOR_SIZE);

	if (unlikely(size_sector & ((PAGE_SIZE / SECTOR_SIZE) - 1)))
		page_count += 1;

	return page_count;
}

static sector_t _blk_deferred_submit_pages(struct block_device *blk_dev,
				    struct blk_deferred_request *dio_req, int direction,
				    sector_t arr_ofs, struct page **page_array, sector_t ofs_sector,
				    sector_t size_sector)
{
	struct bio *bio = NULL;
	int nr_iovecs;
	int page_inx = arr_ofs >> (PAGE_SHIFT - SECTOR_SHIFT);
	sector_t process_sect = 0;

	nr_iovecs = _page_count_calculate(size_sector);

	while (NULL == (bio = _blk_deferred_bio_alloc(nr_iovecs))) {
		size_sector = (size_sector >> 1) & ~((PAGE_SIZE / SECTOR_SIZE) - 1);
		if (size_sector == 0)
			return 0;

		nr_iovecs = _page_count_calculate(size_sector);
	}

	bio_set_dev(bio, blk_dev);

	if (direction == READ)
		bio_set_op_attrs(bio, REQ_OP_READ, 0);
	else
		bio_set_op_attrs(bio, REQ_OP_WRITE, 0);

	bio->bi_iter.bi_sector = ofs_sector;

	{ //add first
		sector_t unordered = arr_ofs & ((PAGE_SIZE / SECTOR_SIZE) - 1);
		sector_t bvec_len_sect =
			min_t(sector_t, ((PAGE_SIZE / SECTOR_SIZE) - unordered), size_sector);
		struct page *page = page_array[page_inx];
		unsigned int len = (unsigned int)from_sectors(bvec_len_sect);
		unsigned int offset = (unsigned int)from_sectors(unordered);

		if (unlikely(page == NULL)) {
			pr_err("NULL found in page array");
			bio_put(bio);
			return 0;
		}
		if (unlikely(bio_add_page(bio, page, len, offset) != len)) {
			bio_put(bio);
			return 0;
		}
		++page_inx;
		process_sect += bvec_len_sect;
	}

	while (process_sect < size_sector) {
		sector_t bvec_len_sect =
			min_t(sector_t, (PAGE_SIZE / SECTOR_SIZE), (size_sector - process_sect));
		struct page *page = page_array[page_inx];
		unsigned int len = (unsigned int)from_sectors(bvec_len_sect);


		if (unlikely(page == NULL)) {
			pr_err("NULL found in page array");
			break;
		}
		if (unlikely(bio_add_page(bio, page, len, 0) != len))
			break;

		++page_inx;
		process_sect += bvec_len_sect;
	}

	((struct dio_bio_complete *)bio->bi_private)->dio_req = dio_req;
	((struct dio_bio_complete *)bio->bi_private)->bio_sect_len = process_sect;

	submit_bio_direct(bio);

	return process_sect;
}

sector_t blk_deferred_submit_pages(struct block_device *blk_dev,
				   struct blk_deferred_request *dio_req, int direction,
				   sector_t arr_ofs, struct page **page_array, sector_t ofs_sector,
				   sector_t size_sector)
{
	sector_t process_sect = 0;

	do {
		sector_t portion_sect = _blk_deferred_submit_pages(
			blk_dev, dio_req, direction, arr_ofs + process_sect, page_array,
			ofs_sector + process_sect, size_sector - process_sect);
		if (portion_sect == 0) {
			pr_err("Failed to submit defer IO pages. Only [%lld] sectors processed\n",
			       process_sect);
			break;
		}
		process_sect += portion_sect;
	} while (process_sect < size_sector);

	return process_sect;
}

struct blk_deferred_request *blk_deferred_request_new(void)
{
	struct blk_deferred_request *dio_req = NULL;

	dio_req = kzalloc(sizeof(struct blk_deferred_request), GFP_NOIO);
	if (dio_req == NULL)
		return NULL;

	INIT_LIST_HEAD(&dio_req->dios);

	dio_req->result = SUCCESS;
	atomic64_set(&dio_req->sect_processed, 0);
	dio_req->sect_len = 0;
	init_completion(&dio_req->complete);

	return dio_req;
}

bool blk_deferred_request_already_added(struct blk_deferred_request *dio_req,
					unsigned long block_index)
{
	bool result = false;
	struct list_head *_list_head;

	if (list_empty(&dio_req->dios))
		return result;

	list_for_each(_list_head, &dio_req->dios) {
		struct blk_deferred_io *dio = list_entry(_list_head, struct blk_deferred_io, link);

		if (dio->blk_index == block_index) {
			result = true;
			break;
		}
	}

	return result;
}

int blk_deferred_request_add(struct blk_deferred_request *dio_req, struct blk_deferred_io *dio)
{
	list_add_tail(&dio->link, &dio_req->dios);
	dio_req->sect_len += dio->sect.cnt;

	return SUCCESS;
}

void blk_deferred_request_free(struct blk_deferred_request *dio_req)
{
	if (dio_req != NULL) {
		while (!list_empty(&dio_req->dios)) {
			struct blk_deferred_io *dio =
				list_entry(dio_req->dios.next, struct blk_deferred_io, link);

			list_del(&dio->link);

			blk_deferred_free(dio);
		}
		kfree(dio_req);
	}
}

void blk_deferred_request_waiting_skip(struct blk_deferred_request *dio_req)
{
	init_completion(&dio_req->complete);
	atomic64_set(&dio_req->sect_processed, 0);
}

int blk_deferred_request_wait(struct blk_deferred_request *dio_req)
{
	u64 start_jiffies = get_jiffies_64();
	u64 current_jiffies;

	while (wait_for_completion_timeout(&dio_req->complete, (HZ * 1)) == 0) {
		current_jiffies = get_jiffies_64();
		if (jiffies_to_msecs(current_jiffies - start_jiffies) > 60 * 1000) {
			pr_warn("Defer IO request timeout\n");
			return -EDEADLK;
		}
	}

	return dio_req->result;
}

int blk_deferred_request_read_original(struct block_device *original_blk_dev,
				       struct blk_deferred_request *dio_copy_req)
{
	int res = -ENODATA;
	struct list_head *_list_head;

	blk_deferred_request_waiting_skip(dio_copy_req);

	if (list_empty(&dio_copy_req->dios))
		return res;

	list_for_each(_list_head, &dio_copy_req->dios) {
		struct blk_deferred_io *dio = list_entry(_list_head, struct blk_deferred_io, link);

		sector_t ofs = dio->sect.ofs;
		sector_t cnt = dio->sect.cnt;

		if (cnt != blk_deferred_submit_pages(original_blk_dev, dio_copy_req, READ, 0,
						     dio->page_array, ofs, cnt)) {
			pr_err("Failed to submit reading defer IO request. offset=%lld\n",
			       dio->sect.ofs);
			res = -EIO;
			break;
		}
		res = SUCCESS;
	}

	if (res == SUCCESS)
		res = blk_deferred_request_wait(dio_copy_req);

	return res;
}


static int _store_file(struct block_device *blk_dev, struct blk_deferred_request *dio_copy_req,
		       struct blk_descr_file *blk_descr, struct page **page_array)
{
	struct list_head *_rangelist_head;
	sector_t page_array_ofs = 0;

	if (unlikely(list_empty(&blk_descr->rangelist))) {
		pr_err("Invalid block descriptor");
		return -EINVAL;
	}
	list_for_each(_rangelist_head, &blk_descr->rangelist) {
		struct blk_range_link *range_link;
		sector_t process_sect;

		range_link = list_entry(_rangelist_head, struct blk_range_link, link);
		process_sect = blk_deferred_submit_pages(blk_dev, dio_copy_req, WRITE,
							 page_array_ofs, page_array,
							 range_link->rg.ofs, range_link->rg.cnt);
		if (range_link->rg.cnt != process_sect) {
			pr_err("Failed to submit defer IO request for storing\n");
			return -EIO;
		}
		page_array_ofs += range_link->rg.cnt;
	}
	return SUCCESS;
}

int blk_deferred_request_store_file(struct block_device *blk_dev,
				    struct blk_deferred_request *dio_copy_req)
{
	struct list_head *_dio_list_head;

	blk_deferred_request_waiting_skip(dio_copy_req);

	if (unlikely(list_empty(&dio_copy_req->dios))) {
		pr_err("Invalid deferred io request");
		return -EINVAL;
	}
	list_for_each(_dio_list_head, &dio_copy_req->dios) {
		int res;
		struct blk_deferred_io *dio;

		dio = list_entry(_dio_list_head, struct blk_deferred_io, link);
		res = _store_file(blk_dev, dio_copy_req, dio->blk_descr.file, dio->page_array);
		if (res != SUCCESS)
			return res;
	}

	return blk_deferred_request_wait(dio_copy_req);
}

#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV

static int _store_multidev(struct blk_deferred_request *dio_copy_req,
			   struct blk_descr_multidev *blk_descr, struct page **page_array)
{
	struct list_head *_ranges_list_head;
	sector_t page_array_ofs = 0;

	if (unlikely(list_empty(&blk_descr->rangelist))) {
		pr_err("Invalid block descriptor");
		return -EINVAL;
	}
	list_for_each(_ranges_list_head, &blk_descr->rangelist) {
		sector_t process_sect;
		struct blk_range_link_ex *range_link;

		range_link = list_entry(_ranges_list_head, struct blk_range_link_ex, link);
		process_sect = blk_deferred_submit_pages(range_link->blk_dev, dio_copy_req, WRITE,
							 page_array_ofs, page_array,
							 range_link->rg.ofs, range_link->rg.cnt);
		if (range_link->rg.cnt != process_sect) {
			pr_err("Failed to submit defer IO request for storing\n");
			return -EIO;
		}

		page_array_ofs += range_link->rg.cnt;
	}

	return SUCCESS;
}

int blk_deferred_request_store_multidev(struct blk_deferred_request *dio_copy_req)
{
	struct list_head *_dio_list_head;

	blk_deferred_request_waiting_skip(dio_copy_req);

	if (unlikely(list_empty(&dio_copy_req->dios))) {
		pr_err("Invalid deferred io request");
		return -EINVAL;
	}
	list_for_each(_dio_list_head, &dio_copy_req->dios) {
		int res;
		struct blk_deferred_io *dio;

		dio = list_entry(_dio_list_head, struct blk_deferred_io, link);
		res = _store_multidev(dio_copy_req, dio->blk_descr.multidev, dio->page_array);
		if (res != SUCCESS)
			return res;
	}

	return blk_deferred_request_wait(dio_copy_req);
}
#endif

static size_t _store_pages(void *dst, struct page **page_array, size_t length)
{
	size_t page_inx = 0;
	size_t processed_len = 0;

	while ((processed_len < length) && (page_array[page_inx] != NULL)) {
		void *src;
		size_t page_len = min_t(size_t, PAGE_SIZE, (length - processed_len));

		src = page_address(page_array[page_inx]);
		memcpy(dst + processed_len, src, page_len);

		++page_inx;
		processed_len += page_len;
	}

	return processed_len;
}

int blk_deferred_request_store_mem(struct blk_deferred_request *dio_copy_req)
{
	int res = SUCCESS;
	sector_t processed = 0;

	if (!list_empty(&dio_copy_req->dios)) {
		struct list_head *_list_head;

		list_for_each(_list_head, &dio_copy_req->dios) {
			size_t length;
			size_t portion;
			struct blk_deferred_io *dio;

			dio = list_entry(_list_head, struct blk_deferred_io, link);
			length = snapstore_block_size() * SECTOR_SIZE;

			portion = _store_pages(dio->blk_descr.mem->buff, dio->page_array, length);
			if (unlikely(portion != length)) {
				res = -EIO;
				break;
			}
			processed += (sector_t)to_sectors(portion);
		}
	}

	blk_deferred_complete(dio_copy_req, processed, res);
	return res;
}
