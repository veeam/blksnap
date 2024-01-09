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

int bdevfilter_register(struct bdevfilter_operations *fops);
void bdevfilter_unregister(struct bdevfilter_operations *fops);

#if defined(HAVE_QC_SUBMIT_BIO_NOACCT)
blk_qc_t bdevfilter_resubmit_bio(struct bio *bio, struct blkfilter *flt);
#elif defined(HAVE_VOID_SUBMIT_BIO_NOACCT)
void bdevfilter_resubmit_bio(struct bio *bio, struct blkfilter *flt);
#endif


#endif /* __BDEVFILTER_INTERNAL_H */

