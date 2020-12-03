// SPDX-License-Identifier: GPL-2.0
#include "common.h"
#include "blk_util.h"

const fmode_t fmode = FMODE_READ | FMODE_WRITE;
struct bio_set blk_snap_bioset = { 0 };

int blk_dev_open(dev_t dev_id, struct block_device **p_blk_dev)
{

	struct block_device *blk_dev;
#if 0
	int result = SUCCESS;
	int refCount;

	blk_dev = bdget(dev_id);
	if (blk_dev == NULL) {
		pr_err("Unable to open device [%d:%d]: bdget return NULL\n", MAJOR(dev_id),
		       MINOR(dev_id));
		return -ENODEV;
	}

	refCount = blkdev_get(blk_dev, fmode, NULL);
	if (refCount < 0) {
		pr_err("Unable to open device [%d:%d]: blkdev_get return error code %d\n",
		       MAJOR(dev_id), MINOR(dev_id), 0 - refCount);
		result = refCount;
	}

	if (result == SUCCESS)
		*p_blk_dev = blk_dev;
	return result;
#else
	bdev = blkdev_get_by_dev(dev_id, fmode, NULL);
	if (IS_ERR(bdev))
		return PTR_ERR(bdev);

	*p_blk_dev = blk_dev;
	return 0;
#endif
}

void blk_dev_close(struct block_device *blk_dev)
{
	blkdev_put(blk_dev, fmode);
}


int blk_bioset_create(void)
{
	return bioset_init(&blk_snap_bioset, 64, 0,
			   BIOSET_NEED_BVECS | BIOSET_NEED_RESCUER);
}

void blk_bioset_free(void)
{
	bioset_exit(&blk_snap_bioset);
}

static inline size_t _page_count_calculate(sector_t size_sector)
{
	size_t page_count = size_sector / (PAGE_SIZE / SECTOR_SIZE);

	if (unlikely(size_sector & ((PAGE_SIZE / SECTOR_SIZE) - 1)))
		page_count += 1;

	return page_count;
}

static sector_t _blk_prepare_bio(struct block_device *blk_dev, int direction, sector_t arr_ofs,
				  struct page **page_array,
				  sector_t ofs_sector, sector_t size_sector,
				  struct bio_list *bl)
{
	struct bio *bio = NULL;
	int nr_iovecs;
	int page_inx = arr_ofs >> (PAGE_SHIFT - SECTOR_SHIFT);
	sector_t process_sect = 0;

	nr_iovecs = _page_count_calculate(size_sector);
	bio = bio_alloc_bioset(GFP_NOIO, nr_iovecs, &blk_snap_bioset);
	while (!bio) {
		size_sector = (size_sector >> 1) & ~((PAGE_SIZE / SECTOR_SIZE) - 1);
		if (size_sector == 0)
			return 0;

		nr_iovecs = _page_count_calculate(size_sector);
		bio = bio_alloc_bioset(GFP_NOIO, nr_iovecs, &blk_snap_bioset);
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

	bio_list_add(bl, bio);

	return process_sect;
}

int blk_submit_pages(struct block_device *blk_dev, int direction, sector_t arr_ofs,
			  struct page **page_array,
			  sector_t ofs_sector, sector_t size_sector,
			  atomic_t *bio_counter,
			  void* bi_private, bio_end_io_t bi_end_io)
{
	int ret = 0;
	struct bio *bio = NULL;
	struct bio_list bl = BIO_EMPTY_LIST;
	sector_t process_sect = 0;


	do {
		sector_t portion_sect = _blk_submit_bio(
			blk_dev, direction, arr_ofs + process_sect, page_array,
			ofs_sector + process_sect, size_sector - process_sect,
			);
		if (portion_sect == 0) {
			pr_err("Failed to submit defer IO pages. Only [%lld] sectors processed\n",
			       process_sect);
			ret = -EIO;
			break;
		}

		if (bio_counter)
			atimic_inc(bio_counter);
		process_sect += portion_sect;
	} while (process_sect < size_sector);

	while((bio = bio_list_pop(&bl)) != NULL) {
		if (ret) /* cleanup list */
			put_bio(bio);
		else {   /* submit */
			bio->bi_private = bi_private;
			bio->bi_end_io = bi_end_io;

			bio_set_flag(bio, BIO_INTERPOSED); /* bio should not be interposed */
			submit_bio(bio);
		}
	}

	return ret;
}
