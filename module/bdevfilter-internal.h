/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023 Veeam Software Group GmbH */
#ifndef __BDEVFILTER_INTERNAL_H
#define __BDEVFILTER_INTERNAL_H

#include <linux/types.h>
#include <linux/bio.h>

struct bio;
struct block_device;
struct bdevfilter_operations;

/**
 * struct blkfilter - Block device filter.
 *
 * @fops:	Block device filter operations.
 *
 * For each filtered block device, the filter creates a data structure
 * associated with this device. The data in this structure is specific to the
 * filter, but it must contain a pointer to the block device filter account.
 */
struct blkfilter {
	struct kref kref;
	const struct bdevfilter_operations *fops;

	struct percpu_ref freeze_ref;
	struct wait_queue_head freeze_wq;
	bool is_frozen;
};

/**
 * struct bdevfilter_operations - Block device filter operations.
 *
 * @link:	Entry in the global list of filter drivers
 *		(must not be accessed by the driver).
 * @owner:	Module implementing the filter driver.
 * @name:	Name of the filter driver.
 * @attach:	Attach the filter driver to the block device.
 * @detach:	Detach the filter driver from the block device.
 * @ctl:	Send a control command to the filter driver.
 * @submit_bio:	Handle bio submissions to the filter driver.
 */
struct bdevfilter_operations {
	struct list_head link;
	struct module *owner;
	const char *name;
	struct blkfilter *(*attach)(struct block_device *bdev);
	void (*detach)(struct blkfilter *flt);
	int (*ctl)(struct blkfilter *flt, const unsigned int cmd,
		   __u8 __user *buf, __u32 *plen);
	bool (*submit_bio)(struct bio *bio, struct blkfilter *flt);
};

static inline struct blkfilter *bdevfilter_get(struct blkfilter *flt)
{
        kref_get(&flt->kref);
        return flt;
};
void bdevfilter_free(struct kref *kref);
static inline void bdevfilter_put(struct blkfilter *flt)
{
        if (likely(flt))
                kref_put(&flt->kref, bdevfilter_free);
};

void bdevfilter_detach_all(struct bdevfilter_operations *fops);
int bdevfilter_register(struct bdevfilter_operations *fops);
void bdevfilter_unregister(struct bdevfilter_operations *fops);

#if defined(HAVE_QC_SUBMIT_BIO_NOACCT)
blk_qc_t submit_bio_noacct_notrace(struct bio *bio);
#elif defined(HAVE_VOID_SUBMIT_BIO_NOACCT)
void submit_bio_noacct_notrace(struct bio *bio);
#endif

static inline bool bdevfilter_try_enter(struct blkfilter *flt)
{
	bool ret = false;

	if (percpu_ref_tryget_live(&flt->freeze_ref))
		ret = !flt->is_frozen;
	return ret;
};

static inline void bdevfilter_enter(struct blkfilter *flt)
{
	while (!bdevfilter_try_enter(flt)) {
		smp_rmb();
		wait_event(flt->freeze_wq, !flt->is_frozen);
	}
};

static inline void bdevfilter_exit(struct blkfilter *flt)
{
	percpu_ref_put(&flt->freeze_ref);
};

static inline void bdevfilter_freeze(struct blkfilter *flt)
{
	percpu_ref_kill(&flt->freeze_ref);
	wait_event(flt->freeze_wq, percpu_ref_is_zero(&flt->freeze_ref));
	flt->is_frozen = true;

};

static inline void bdevfilter_unfreeze(struct blkfilter *flt)
{
	flt->is_frozen = false;
	percpu_ref_resurrect(&flt->freeze_ref);
	wake_up_all(&flt->freeze_wq);
};
#endif /* __BDEVFILTER_INTERNAL_H */

