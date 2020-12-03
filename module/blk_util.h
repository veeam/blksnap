/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

int blk_dev_open(dev_t dev_id, struct block_device **p_blk_dev);
void blk_dev_close(struct block_device *blk_dev);

/*
 * this function was copied from block/blk.h
 */
static inline sector_t part_nr_sects_read(struct hd_struct *part)
{
#if (BITS_PER_LONG == 32) && defined(CONFIG_SMP)
	sector_t nr_sects;
	unsigned int seq;

	do {
		seq = read_seqcount_begin(&part->nr_sects_seq);
		nr_sects = part->nr_sects;
	} while (read_seqcount_retry(&part->nr_sects_seq, seq));

	return nr_sects;
#elif (BITS_PER_LONG == 32) && defined(CONFIG_PREEMPTION)
	sector_t nr_sects;

	preempt_disable();
	nr_sects = part->nr_sects;
	preempt_enable();

	return nr_sects;
#else
	return part->nr_sects;
#endif
}

int blk_bioset_create(void);
void blk_bioset_free(void);

int blk_submit_pages(struct block_device *blk_dev, int direction, sector_t arr_ofs,
		     struct page **page_array,
		     sector_t ofs_sector, sector_t size_sector,
		     atomic_t *bio_counter,
		     void* bi_private, bio_end_io_t bi_end_io);
