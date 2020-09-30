/* SPDX-License-Identifier: GPL-2.0 */
#pragma once

#include "rangevector.h"
#include "blk_deferred.h"
#include "blk_redirect.h"
#include "snapstore.h"
#include <linux/xarray.h>
#include <linux/kref.h>

struct snapstore_device {
	struct list_head link;
	struct kref refcount;

	dev_t dev_id;
	struct snapstore *snapstore;

	struct block_device *orig_blk_dev;

	struct xarray store_block_map; // map block index to read block offset
	struct mutex store_block_map_locker;

	struct rangevector zero_sectors;

	atomic_t req_failed_cnt;
	int err_code;
	bool corrupted;
};

void snapstore_device_done(void);

struct snapstore_device *snapstore_device_get_resource(struct snapstore_device *snapstore_device);
void snapstore_device_put_resource(struct snapstore_device *snapstore_device);

struct snapstore_device *snapstore_device_find_by_dev_id(dev_t dev_id);

int snapstore_device_create(dev_t dev_id, struct snapstore *snapstore);

int snapstore_device_cleanup(uuid_t *id);

int snapstore_device_prepare_requests(struct snapstore_device *snapstore_device,
				      struct blk_range *copy_range,
				      struct blk_deferred_request **dio_copy_req);
int snapstore_device_store(struct snapstore_device *snapstore_device,
			   struct blk_deferred_request *dio_copy_req);

int snapstore_device_read(struct snapstore_device *snapstore_device,
			  struct blk_redirect_bio *rq_redir); //request from image
int snapstore_device_write(struct snapstore_device *snapstore_device,
			   struct blk_redirect_bio *rq_redir); //request from image

bool snapstore_device_is_corrupted(struct snapstore_device *snapstore_device);
void snapstore_device_set_corrupted(struct snapstore_device *snapstore_device, int err_code);
int snapstore_device_errno(dev_t dev_id, int *p_err_code);

static inline void _snapstore_device_descr_read_lock(struct snapstore_device *snapstore_device)
{
	mutex_lock(&snapstore_device->store_block_map_locker);
}
static inline void _snapstore_device_descr_read_unlock(struct snapstore_device *snapstore_device)
{
	mutex_unlock(&snapstore_device->store_block_map_locker);
}
