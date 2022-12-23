/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_BDEVFILTER_H
#define __LINUX_BDEVFILTER_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/blk_types.h>
#include <linux/kref.h>

struct bdev_filter;
struct bdev_filter_operations {
	bool (*submit_bio)(struct bio *bio, struct bdev_filter *flt);
	/*
	bool (*read_page_cb)(struct block_device *bdev,
			     sector_t sector, struct page *page,
			     struct bdev_filter *flt);
	bool (*write_page_cb)(struct block_device *bdev,
			      sector_t sector, struct page *page,
			      struct bdev_filter *flt);
	*/
	void (*release)(struct bdev_filter *flt);
};

/**
 * struct bdev_filter - Description of the block device filter.
 * @kref:
 *
 * @fops:
 *
 */
struct bdev_filter {
	struct kref kref;
	const struct bdev_filter_operations *fops;
	struct percpu_rw_semaphore submit_lock;
};

static inline void bdev_filter_init(struct bdev_filter *flt,
		const struct bdev_filter_operations *fops)
{
	kref_init(&flt->kref);
	flt->fops = fops;
	percpu_init_rwsem(&flt->submit_lock);
};
void bdev_filter_free(struct kref *kref);

int bdev_filter_attach(struct block_device *bdev, struct bdev_filter *flt);
void bdev_filter_detach(struct block_device *bdev);
struct bdev_filter *bdev_filter_get_by_bdev(struct block_device *bdev);
static inline void bdev_filter_get(struct bdev_filter *flt)
{
	kref_get(&flt->kref);
};
static inline void bdev_filter_put(struct bdev_filter *flt)
{
	if (likely(flt))
		kref_put(&flt->kref, bdev_filter_free);
};

/* Only for livepatch version */
int lp_bdev_filter_detach(const dev_t dev_id);

#if defined(HAVE_QC_SUBMIT_BIO_NOACCT)
extern blk_qc_t (*submit_bio_noacct_notrace)(struct bio *);
#elif defined(HAVE_VOID_SUBMIT_BIO_NOACCT)
extern void (*submit_bio_noacct_notrace)(struct bio *);
#endif


static inline
void bdevfilter_freeze_queue(struct bdev_filter *flt)
{
	pr_debug("Freeze filtered queue.\n");
	percpu_down_write(&flt->submit_lock);
};
static inline
void bdevfilter_unfreeze_queue(struct bdev_filter *flt)
{
	percpu_up_write(&flt->submit_lock);
	pr_debug("Filtered queue was unfrozen.\n");
};
#endif /* __LINUX_BDEVFILTER_H */
