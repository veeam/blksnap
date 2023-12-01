/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023 Veeam Software Group GmbH */
#ifndef __BDEVFILTER_INTERNAL_H
#define __BDEVFILTER_INTERNAL_H

#include <linux/types.h>

struct bio;
struct block_device;
struct bdevfilter_operations;

/**
 * struct bdevfilter - Block device filter.
 *
 * @ops:	Block device filter operations.
 *
 * For each filtered block device, the filter creates a data structure
 * associated with this device. The data in this structure is specific to the
 * filter, but it must contain a pointer to the block device filter account.
 */
struct bdevfilter {
	struct kref kref;
	const struct bdevfilter_operations *ops;
};

static inline struct bdevfilter *bdevfilter_get(struct bdevfilter *flt)
{
	kref_get(&flt->kref);
	return flt;
};
static inline void bdevfilter_put(struct bdevfilter *flt)
{
	if (likely(flt))
		kref_put(&flt->kref, flt->fops->detach_cb);
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
	struct bdevfilter *(*attach)(struct block_device *bdev);
	void (*detach)(struct bdevfilter *flt);
	int (*ctl)(struct bdevfilter *flt, const unsigned int cmd,
		   __u8 __user *buf, __u32 *plen);
	bool (*submit_bio)(struct bio *bio, struct bdevfilter *flt);
};

int bdevfilter_register(struct bdevfilter_operations *ops);
void bdevfilter_unregister(struct bdevfilter_operations *ops);

notrace blk_qc_t submit_bio_noacct_notrace(struct bio *bio);

#endif /* __BDEVFILTER_INTERNAL_H */

