// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-snapstore"
#include "common.h"
#include "snapstore_device.h"
#include "snapstore.h"
#include "snapstore_blk.h"
#include "blk_util.h"

int inc_snapstore_block_size_pow(void);

LIST_HEAD(snapstore_devices);
DECLARE_RWSEM(snapstore_devices_lock);

static inline void _snapstore_device_descr_write_lock(struct snapstore_device *snapstore_device)
{
	mutex_lock(&snapstore_device->store_block_map_locker);
}
static inline void _snapstore_device_descr_write_unlock(struct snapstore_device *snapstore_device)
{
	mutex_unlock(&snapstore_device->store_block_map_locker);
}

void snapstore_device_done(void)
{
	struct snapstore_device *snapstore_device = NULL;

	do {
		down_write(&snapstore_devices_lock);
		if (!list_empty(&snapstore_devices)) {
			snapstore_device =
				list_entry(snapstore_devices.next, struct snapstore_device, link);
			list_del(&snapstore_device->link);
		}
		up_write(&snapstore_devices_lock);

		if (snapstore_device)
			snapstore_device_put_resource(snapstore_device);
	} while (snapstore_device);
}

struct snapstore_device *snapstore_device_find_by_dev_id(dev_t dev_id)
{
	struct snapstore_device *result = NULL;

	down_read(&snapstore_devices_lock);
	if (!list_empty(&snapstore_devices)) {
		struct list_head *_head;

		list_for_each (_head, &snapstore_devices) {
			struct snapstore_device *snapstore_device =
				list_entry(_head, struct snapstore_device, link);

			if (dev_id == snapstore_device->dev_id) {
				result = snapstore_device;
				break;
			}
		}
	}
	up_read(&snapstore_devices_lock);

	return result;
}

struct snapstore_device *_snapstore_device_get_by_snapstore_id(uuid_t *id)
{
	struct snapstore_device *result = NULL;

	down_write(&snapstore_devices_lock);
	if (!list_empty(&snapstore_devices)) {
		struct list_head *_head;

		list_for_each (_head, &snapstore_devices) {
			struct snapstore_device *snapstore_device =
				list_entry(_head, struct snapstore_device, link);

			if (uuid_equal(id, &snapstore_device->snapstore->id)) {
				result = snapstore_device;
				list_del(&snapstore_device->link);
				break;
			}
		}
	}
	up_write(&snapstore_devices_lock);

	return result;
}

static void _snapstore_device_destroy(struct snapstore_device *snapstore_device)
{
	pr_info("Destroy snapstore device\n");

	xa_destroy(&snapstore_device->store_block_map);

	if (snapstore_device->orig_blk_dev != NULL)
		blk_dev_close(snapstore_device->orig_blk_dev);

	rangevector_done(&snapstore_device->zero_sectors);

	if (snapstore_device->snapstore) {
		pr_info("Snapstore uuid %pUB\n", &snapstore_device->snapstore->id);

		snapstore_put(snapstore_device->snapstore);
		snapstore_device->snapstore = NULL;
	}

	kfree(snapstore_device);
}

static void snapstore_device_free_cb(struct kref *kref)
{
	struct snapstore_device *snapstore_device =
		container_of(kref, struct snapstore_device, refcount);

	_snapstore_device_destroy(snapstore_device);
}

struct snapstore_device *snapstore_device_get_resource(struct snapstore_device *snapstore_device)
{
	if (snapstore_device)
		kref_get(&snapstore_device->refcount);

	return snapstore_device;
};

void snapstore_device_put_resource(struct snapstore_device *snapstore_device)
{
	if (snapstore_device)
		kref_put(&snapstore_device->refcount, snapstore_device_free_cb);
};

int snapstore_device_cleanup(uuid_t *id)
{
	int result = SUCCESS;
	struct snapstore_device *snapstore_device = NULL;

	while (NULL != (snapstore_device = _snapstore_device_get_by_snapstore_id(id))) {
		pr_info("Cleanup snapstore device for device [%d:%d]\n",
			MAJOR(snapstore_device->dev_id), MINOR(snapstore_device->dev_id));

		snapstore_device_put_resource(snapstore_device);
	}
	return result;
}

int snapstore_device_create(dev_t dev_id, struct snapstore *snapstore)
{
	int res = SUCCESS;
	struct snapstore_device *snapstore_device =
		kzalloc(sizeof(struct snapstore_device), GFP_KERNEL);

	if (NULL == snapstore_device)
		return -ENOMEM;

	INIT_LIST_HEAD(&snapstore_device->link);
	snapstore_device->dev_id = dev_id;

	res = blk_dev_open(dev_id, &snapstore_device->orig_blk_dev);
	if (res != SUCCESS) {
		kfree(snapstore_device);

		pr_err("Unable to create snapstore device: failed to open original device [%d:%d]\n",
		       MAJOR(dev_id), MINOR(dev_id));
		return res;
	}

	kref_init(&snapstore_device->refcount);

	snapstore_device->snapstore = NULL;
	snapstore_device->err_code = SUCCESS;
	snapstore_device->corrupted = false;
	atomic_set(&snapstore_device->req_failed_cnt, 0);

	mutex_init(&snapstore_device->store_block_map_locker);

	rangevector_init(&snapstore_device->zero_sectors);

	xa_init(&snapstore_device->store_block_map);

	snapstore_device->snapstore = snapstore_get(snapstore);

	down_write(&snapstore_devices_lock);
	list_add_tail(&snapstore_device->link, &snapstore_devices);
	up_write(&snapstore_devices_lock);

	return SUCCESS;
}

int snapstore_device_add_request(struct snapstore_device *snapstore_device,
				 unsigned long block_index,
				 struct blk_deferred_request **dio_copy_req)
{
	int res = SUCCESS;
	union blk_descr_unify blk_descr = { NULL };
	struct blk_deferred_io *dio = NULL;
	bool req_new = false;

	blk_descr = snapstore_get_empty_block(snapstore_device->snapstore);
	if (blk_descr.ptr == NULL) {
		pr_err("Unable to add block to defer IO request: failed to allocate next block\n");
		return -ENODATA;
	}

	res = xa_err(
		xa_store(&snapstore_device->store_block_map, block_index, blk_descr.ptr, GFP_NOIO));
	if (res != SUCCESS) {
		pr_err("Unable to add block to defer IO request: failed to set block descriptor to descriptors array. errno=%d\n",
		       res);
		return res;
	}

	if (*dio_copy_req == NULL) {
		*dio_copy_req = blk_deferred_request_new();
		if (*dio_copy_req == NULL) {
			pr_err("Unable to add block to defer IO request: failed to allocate defer IO request\n");
			return -ENOMEM;
		}
		req_new = true;
	}

	do {
		dio = blk_deferred_alloc(block_index, blk_descr);
		if (dio == NULL) {
			pr_err("Unabled to add block to defer IO request: failed to allocate defer IO\n");
			res = -ENOMEM;
			break;
		}

		res = blk_deferred_request_add(*dio_copy_req, dio);
		if (res != SUCCESS)
			pr_err("Unable to add block to defer IO request: failed to add defer IO to request\n");
	} while (false);

	if (res != SUCCESS) {
		if (dio != NULL) {
			blk_deferred_free(dio);
			dio = NULL;
		}
		if (req_new) {
			blk_deferred_request_free(*dio_copy_req);
			*dio_copy_req = NULL;
		}
	}

	return res;
}

int snapstore_device_prepare_requests(struct snapstore_device *snapstore_device,
				      struct blk_range *copy_range,
				      struct blk_deferred_request **dio_copy_req)
{
	int res = SUCCESS;
	unsigned long inx = 0;
	unsigned long first = (unsigned long)(copy_range->ofs >> snapstore_block_shift());
	unsigned long last =
		(unsigned long)((copy_range->ofs + copy_range->cnt - 1) >> snapstore_block_shift());

	for (inx = first; inx <= last; inx++) {
		if (NULL != xa_load(&snapstore_device->store_block_map, inx)) {
			//Already stored block
		} else {
			res = snapstore_device_add_request(snapstore_device, inx, dio_copy_req);
			if (res != SUCCESS) {
				pr_err("Failed to create copy defer IO request. errno=%d\n", res);
				break;
			}
		}
	}
	if (res != SUCCESS) {
		snapstore_device_set_corrupted(snapstore_device, res);
	}

	return res;
}

int snapstore_device_store(struct snapstore_device *snapstore_device,
			   struct blk_deferred_request *dio_copy_req)
{
	int res = snapstore_request_store(snapstore_device->snapstore, dio_copy_req);
	if (res != SUCCESS)
		snapstore_device_set_corrupted(snapstore_device, res);

	return res;
}

int snapstore_device_read(struct snapstore_device *snapstore_device,
			  struct blk_redirect_bio *rq_redir)
{
	int res = SUCCESS;

	unsigned long block_index;
	unsigned long block_index_last;
	unsigned long block_index_first;

	sector_t blk_ofs_start = 0; //device range start
	sector_t blk_ofs_count = 0; //device range length

	struct blk_range rq_range;
	struct rangevector *zero_sectors = &snapstore_device->zero_sectors;

	if (snapstore_device_is_corrupted(snapstore_device))
		return -ENODATA;

	rq_range.cnt = bio_sectors(rq_redir->bio);
	rq_range.ofs = rq_redir->bio->bi_iter.bi_sector;

	if (!bio_has_data(rq_redir->bio)) {
		pr_warn("Empty bio was found during reading from snapstore device. flags=%u\n",
			rq_redir->bio->bi_flags);

		blk_redirect_complete(rq_redir, SUCCESS);
		return SUCCESS;
	}

	block_index_first = (unsigned long)(rq_range.ofs >> snapstore_block_shift());
	block_index_last =
		(unsigned long)((rq_range.ofs + rq_range.cnt - 1) >> snapstore_block_shift());

	_snapstore_device_descr_write_lock(snapstore_device);
	for (block_index = block_index_first; block_index <= block_index_last; ++block_index) {
		union blk_descr_unify blk_descr;

		blk_ofs_count = min_t(sector_t,
				      (((sector_t)(block_index + 1)) << snapstore_block_shift()) -
					      (rq_range.ofs + blk_ofs_start),
				      rq_range.cnt - blk_ofs_start);

		blk_descr = (union blk_descr_unify)xa_load(&snapstore_device->store_block_map,
							   block_index);
		if (blk_descr.ptr) {
			//push snapstore read
			res = snapstore_redirect_read(rq_redir, snapstore_device->snapstore,
						      blk_descr, rq_range.ofs + blk_ofs_start,
						      blk_ofs_start, blk_ofs_count);
			if (res != SUCCESS) {
				pr_err("Failed to read from snapstore device\n");
				break;
			}
		} else {
			//device read with zeroing
			if (zero_sectors)
				res = blk_dev_redirect_read_zeroed(rq_redir,
								   snapstore_device->orig_blk_dev,
								   rq_range.ofs, blk_ofs_start,
								   blk_ofs_count, zero_sectors);
			else
				res = blk_dev_redirect_part(rq_redir, READ,
							    snapstore_device->orig_blk_dev,
							    rq_range.ofs + blk_ofs_start,
							    blk_ofs_start, blk_ofs_count);

			if (res != SUCCESS) {
				pr_err("Failed to redirect read request to the original device [%d:%d]\n",
				       MAJOR(snapstore_device->dev_id),
				       MINOR(snapstore_device->dev_id));
				break;
			}
		}

		blk_ofs_start += blk_ofs_count;
	}

	if (res == SUCCESS) {
		if (atomic64_read(&rq_redir->bio_count) > 0ll) //async direct access needed
			blk_dev_redirect_submit(rq_redir);
		else
			blk_redirect_complete(rq_redir, res);
	} else {
		pr_err("Failed to read from snapstore device. errno=%d\n", res);
		pr_err("Position %lld sector, length %lld sectors\n", rq_range.ofs, rq_range.cnt);
	}
	_snapstore_device_descr_write_unlock(snapstore_device);

	return res;
}

int _snapstore_device_copy_on_write(struct snapstore_device *snapstore_device,
				    struct blk_range *rq_range)
{
	int res = SUCCESS;
	struct blk_deferred_request *dio_copy_req = NULL;

	mutex_lock(&snapstore_device->store_block_map_locker);
	do {
		res = snapstore_device_prepare_requests(snapstore_device, rq_range, &dio_copy_req);
		if (res != SUCCESS) {
			pr_err("Failed to create defer IO request for range. errno=%d\n", res);
			break;
		}

		if (NULL == dio_copy_req)
			break; //nothing to copy

		res = blk_deferred_request_read_original(snapstore_device->orig_blk_dev,
							 dio_copy_req);
		if (res != SUCCESS) {
			pr_err("Failed to read data from the original device. errno=%d\n", res);
			break;
		}

		res = snapstore_device_store(snapstore_device, dio_copy_req);
		if (res != SUCCESS) {
			pr_err("Failed to write data to snapstore. errno=%d\n", res);
			break;
		}
	} while (false);
	mutex_unlock(&snapstore_device->store_block_map_locker);

	if (dio_copy_req) {
		if (res == -EDEADLK)
			blk_deferred_request_deadlocked(dio_copy_req);
		else
			blk_deferred_request_free(dio_copy_req);
	}

	return res;
}

int snapstore_device_write(struct snapstore_device *snapstore_device,
			   struct blk_redirect_bio *rq_redir)
{
	int res = SUCCESS;

	unsigned long block_index;
	unsigned long block_index_last;
	unsigned long block_index_first;

	sector_t blk_ofs_start = 0; //device range start
	sector_t blk_ofs_count = 0; //device range length

	struct blk_range rq_range;

	BUG_ON(NULL == snapstore_device);
	BUG_ON(NULL == rq_redir);
	BUG_ON(NULL == rq_redir->bio);

	if (snapstore_device_is_corrupted(snapstore_device))
		return -ENODATA;

	rq_range.cnt = bio_sectors(rq_redir->bio);
	rq_range.ofs = rq_redir->bio->bi_iter.bi_sector;

	if (!bio_has_data(rq_redir->bio)) {
		pr_warn("Empty bio was found during reading from snapstore device. flags=%u\n",
			rq_redir->bio->bi_flags);

		blk_redirect_complete(rq_redir, SUCCESS);
		return SUCCESS;
	}

	// do copy to snapstore previously
	res = _snapstore_device_copy_on_write(snapstore_device, &rq_range);

	block_index_first = (unsigned long)(rq_range.ofs >> snapstore_block_shift());
	block_index_last =
		(unsigned long)((rq_range.ofs + rq_range.cnt - 1) >> snapstore_block_shift());

	_snapstore_device_descr_write_lock(snapstore_device);
	for (block_index = block_index_first; block_index <= block_index_last; ++block_index) {
		union blk_descr_unify blk_descr;

		blk_ofs_count = min_t(sector_t,
				      (((sector_t)(block_index + 1)) << snapstore_block_shift()) -
					      (rq_range.ofs + blk_ofs_start),
				      rq_range.cnt - blk_ofs_start);

		blk_descr = (union blk_descr_unify)xa_load(&snapstore_device->store_block_map,
							   block_index);
		if (blk_descr.ptr == NULL) {
			pr_err("Unable to write from snapstore device: invalid snapstore block descriptor\n");
			res = -EIO;
			break;
		}

		res = snapstore_redirect_write(rq_redir, snapstore_device->snapstore, blk_descr,
					       rq_range.ofs + blk_ofs_start, blk_ofs_start,
					       blk_ofs_count);
		if (res != SUCCESS) {
			pr_err("Unable to write from snapstore device: failed to redirect write request to snapstore\n");
			break;
		}

		blk_ofs_start += blk_ofs_count;
	}
	if (res == SUCCESS) {
		if (atomic64_read(&rq_redir->bio_count) > 0) { //async direct access needed
			blk_dev_redirect_submit(rq_redir);
		} else {
			blk_redirect_complete(rq_redir, res);
		}
	} else {
		pr_err("Failed to write from snapstore device. errno=%d\n", res);
		pr_err("Position %lld sector, length %lld sectors\n", rq_range.ofs, rq_range.cnt);

		snapstore_device_set_corrupted(snapstore_device, res);
	}
	_snapstore_device_descr_write_unlock(snapstore_device);
	return res;
}

bool snapstore_device_is_corrupted(struct snapstore_device *snapstore_device)
{
	if (snapstore_device == NULL)
		return true;

	if (snapstore_device->corrupted) {
		if (0 == atomic_read(&snapstore_device->req_failed_cnt))
			pr_err("Snapshot device is corrupted for [%d:%d]\n",
			       MAJOR(snapstore_device->dev_id), MINOR(snapstore_device->dev_id));

		atomic_inc(&snapstore_device->req_failed_cnt);
		return true;
	}

	return false;
}

void snapstore_device_set_corrupted(struct snapstore_device *snapstore_device, int err_code)
{
	if (!snapstore_device->corrupted) {
		atomic_set(&snapstore_device->req_failed_cnt, 0);
		snapstore_device->corrupted = true;
		snapstore_device->err_code = abs(err_code);

		pr_err("Set snapshot device is corrupted for [%d:%d]\n",
		       MAJOR(snapstore_device->dev_id), MINOR(snapstore_device->dev_id));
	}
}

int snapstore_device_errno(dev_t dev_id, int *p_err_code)
{
	struct snapstore_device *snapstore_device = snapstore_device_find_by_dev_id(dev_id);
	if (snapstore_device == NULL)
		return -ENODATA;

	*p_err_code = snapstore_device->err_code;
	return SUCCESS;
}
