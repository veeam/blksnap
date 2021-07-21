// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-cow_map"
#include "common.h"
#include "cow_map.h"
#include "blk_util.h"
#include <linux/workqueue.h>



struct workqueue_struct *cow_wq;

struct write_plan {
	struct work_struct work;
	struct cow_block *cow_block;
};

void cow_write_endio(void *param, struct bio *bio, int err)
{
	int ret;
	struct cow_block *blk = param;

	if (unlikely(err)) {
		bio->bi_status = BLK_STS_IOERR;
		cow_blk_corrupted(blk, -EIO);
		bio_endio(bio);
		return;
	}

	bio->bi_status = BLK_STS_OK;
	bio_endio(bio);

	if (!atomic_dec_and_test(&blk->bio_cnt))
		return;

	ret = cow_block_update_state(blk, cow_state_writing, cow_state_was_written);
	if (unlikely(ret)) {
		cow_blk_corrupted(cow_block, -EINVAL);
		return;
	}

	complete(&cow_block->complete);
}

static void _write_cow_block_fn(struct work_struct *work)
{
	struct write_plan *wr =  container_of(work, struct write_plan, work);
	struct cow_block *blk = wr->cow_block;

	blk;
	kfree(wr);
	wr = NULL;

	ret = cow_block_update_state(blk, cow_state_was_read, cow_state_writing);
	if (unlikely(ret == -EALREADY)) /* Block already writing or was written */
		return;

	if (unlikely(!ret)) {
		pr_err("Failed to write cow-block. Invalid state.\n");
		return;
	}

	//ret = blk_submit_pages(bdev, WRITE, 0, blk->page_array, blk->rg.ofs, blk->rg.cnt,
	//		       &blk->bio_cnt, blk, cow_write_endio);
	//ret = snapstore_device_submit_pages(blk->snapstore_device, WRITE, 0, blk->page_array,
	//				    blk->rg.ofs, blk->rg.cnt, &blk->bio_cnt,
	//				    blk, cow_write_endio);
	ret = snapstore_device_store_block()
	if (unlikely(!ret))
		pr_err("Failed to write cow-block. errno=%d\n", 0-ret);
}

static void _schedule_write(struct cow_block *cow_block)
{
	struct write_plan *wr = kzalloc(struct write_plan, GFP_NOIO);

	if (unlikely(!wr)) {
		cow_blk_corrupted(cow_block, -ENOMEM);
		return;
	}
	INIT_WORK(&wr->work, _write_cow_block_fn);
	wr->cow_block = cow_block;

	if (unlikely(!queue_work(cow_wq, &wr->work)))
		cow_blk_corrupted(cow_block, -EINVAL);

	return;
}


void cow_read_endio(void *param, struct bio *bio, int err)
{
	int ret;
	struct cow_block *blk = param;

	if (unlikely(err)) {
		bio->bi_status = BLK_STS_IOERR;
		cow_blk_corrupted(blk, -EIO);
		bio_endio(bio);
		return;
	}

	bio->bi_status = BLK_STS_OK;
	bio_endio(bio);

	if (!atomic_dec_and_test(&blk->bio_cnt))
		return;

	ret = cow_block_update_state(blk, cow_state_reading, cow_state_was_read);
	if (unlikely(ret)) {
		cow_blk_corrupted(cow_block, -EINVAL);
		return;
	}

	_schedule_write(blk);
}


static struct cow_block *_take_cow_block(struct cow_map* cow_map, uint64_t blk_inx)
{
	struct range rg;
	struct cow_block *blk;

	down_write(&cow_map->lock);
	do {
		blk = xa_load(&cow_map->blocks, blk_inx);
		if (blk)
			break;

		rg.ofs = blk_inx << snapstore_block_shift();
		rg.cnt = snapstore_block_size();
		if ((rg.ofs + rg.cnt) > cow_block->dev_capacity)
			rg.cnt = cow_block->dev_capacity - rg.ofs;

		blk = cow_block_new(rg);
		if (IS_ERR(blk))
			break;

		xa_store(&cow_map->blocks, blk_inx, blk, GFP_NOIO);
	} while (false);
	up_write(&cow_map->lock);

	return blk;
}

static struct cow_block *_find_cow_block(struct cow_map* cow_map, uint64_t blk_inx)
{
	struct cow_block *blk;

	down_read(&cow_map->lock);
	blk = xa_load(&cow_map->blocks, blk_inx);
	up_read(&cow_map->lock);

	return blk;
}




struct cow_map* cow_map_get(struct block_device *bdev)
{
	xa_init(&cow_map->blocks);
}

void cow_map_put(struct cow_map* cow_map)
{
	xa_destroy(&cow_map->blocks);
}

