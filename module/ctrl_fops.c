// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-ctrl"
#include "common.h"
#include "blk-snap-ctl.h"
#include "ctrl_fops.h"
#include "version.h"
#include "tracking.h"
#include "snapshot.h"
#include "snapstore.h"
#include "snapimage.h"
#include "tracker.h"
#include "blk_deferred.h"
#include "big_buffer.h"
#include "params.h"

#include <linux/module.h>
#include <linux/poll.h>
#include <linux/uaccess.h>

static int blk_snap_major = 0;

static struct file_operations ctrl_fops = { .owner = THIS_MODULE,
					    .read = ctrl_read,
					    .write = ctrl_write,
					    .open = ctrl_open,
					    .release = ctrl_release,
					    .poll = ctrl_poll,
					    .unlocked_ioctl = ctrl_unlocked_ioctl };

int get_change_tracking_block_size_pow(void);

static atomic_t g_dev_open_cnt = ATOMIC_INIT(0);

static struct ioctl_getversion_s g_version = { .major = FILEVER_MAJOR,
					       .minor = FILEVER_MINOR,
					       .revision = FILEVER_REVISION,
					       .build = 0 };

int get_blk_snap_major(void)
{
	return blk_snap_major;
}

int ctrl_init(void)
{
	int ret;

	ret = register_chrdev(0, MODULE_NAME, &ctrl_fops);
	if (ret < 0) {
		pr_err("Failed to register a character device. errno=%d\n", blk_snap_major);
		return ret;
	}

	blk_snap_major = ret;
	pr_info("Module major [%d]\n", blk_snap_major);
	return SUCCESS;
}

void ctrl_done(void)
{
	unregister_chrdev(blk_snap_major, MODULE_NAME);
	ctrl_pipe_done();
}

ssize_t ctrl_read(struct file *fl, char __user *buffer, size_t length, loff_t *offset)
{
	ssize_t bytes_read = 0;
	struct ctrl_pipe *pipe = (struct ctrl_pipe *)fl->private_data;

	if (pipe == NULL) {
		pr_err("Unable to read from pipe: invalid pipe pointer\n");
		return -EINVAL;
	}

	bytes_read = ctrl_pipe_read(pipe, buffer, length);
	if (bytes_read == 0)
		if (fl->f_flags & O_NONBLOCK)
			bytes_read = -EAGAIN;

	return bytes_read;
}

ssize_t ctrl_write(struct file *fl, const char __user *buffer, size_t length, loff_t *offset)
{
	struct ctrl_pipe *pipe = (struct ctrl_pipe *)fl->private_data;

	if (pipe == NULL) {
		pr_err("Unable to write into pipe: invalid pipe pointer\n");
		return -EINVAL;
	}

	return ctrl_pipe_write(pipe, buffer, length);
}

unsigned int ctrl_poll(struct file *fl, struct poll_table_struct *wait)
{
	struct ctrl_pipe *pipe = (struct ctrl_pipe *)fl->private_data;

	if (pipe == NULL) {
		pr_err("Unable to poll pipe: invalid pipe pointer\n");
		return -EINVAL;
	}

	return ctrl_pipe_poll(pipe);
}

int ctrl_open(struct inode *inode, struct file *fl)
{
	fl->f_pos = 0;

	if (false == try_module_get(THIS_MODULE))
		return -EINVAL;

	fl->private_data = (void *)ctrl_pipe_new();
	if (fl->private_data == NULL) {
		pr_err("Failed to open ctrl file\n");
		return -ENOMEM;
	}

	atomic_inc(&g_dev_open_cnt);

	return SUCCESS;
}

int ctrl_release(struct inode *inode, struct file *fl)
{
	int result = SUCCESS;

	if (atomic_read(&g_dev_open_cnt) > 0) {
		module_put(THIS_MODULE);
		ctrl_pipe_put_resource((struct ctrl_pipe *)fl->private_data);

		atomic_dec(&g_dev_open_cnt);
	} else {
		pr_err("Unable to close ctrl file: the file is already closed\n");
		result = -EALREADY;
	}

	return result;
}

int ioctl_compatibility_flags(unsigned long arg)
{
	unsigned long len;
	struct ioctl_compatibility_flags_s param;

	param.flags = 0;
	param.flags |= BLK_SNAP_COMPATIBILITY_SNAPSTORE;
#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
	param.flags |= BLK_SNAP_COMPATIBILITY_MULTIDEV;
#endif
	len = copy_to_user((void *)arg, &param, sizeof(struct ioctl_compatibility_flags_s));
	if (len != 0) {
		pr_err("Unable to get compatibility flags: invalid user buffer\n");
		return -EINVAL;
	}

	return SUCCESS;
}

int ioctl_get_version(unsigned long arg)
{
	unsigned long len;

	pr_info("Get version\n");

	len = copy_to_user((void *)arg, &g_version, sizeof(struct ioctl_getversion_s));
	if (len != 0) {
		pr_err("Unable to get version: invalid user buffer\n");
		return -ENODATA;
	}

	return SUCCESS;
}

int ioctl_tracking_add(unsigned long arg)
{
	unsigned long len;
	struct ioctl_dev_id_s dev;

	len = copy_from_user(&dev, (void *)arg, sizeof(struct ioctl_dev_id_s));
	if (len != 0) {
		pr_err("Unable to add device under tracking: invalid user buffer\n");
		return -ENODATA;
	}

	return tracking_add(MKDEV(dev.major, dev.minor), get_change_tracking_block_size_pow(),
			    0ull);
}

int ioctl_tracking_remove(unsigned long arg)
{
	struct ioctl_dev_id_s dev;

	if (copy_from_user(&dev, (void *)arg, sizeof(struct ioctl_dev_id_s)) != 0) {
		pr_err("Unable to remove device from tracking: invalid user buffer\n");
		return -ENODATA;
	}
	return tracking_remove(MKDEV(dev.major, dev.minor));
	;
}

int ioctl_tracking_collect(unsigned long arg)
{
	unsigned long len;
	int res;
	struct ioctl_tracking_collect_s get;

	pr_info("Collecting tracking devices:\n");

	len = copy_from_user(&get, (void *)arg, sizeof(struct ioctl_tracking_collect_s));
	if (len  != 0) {
		pr_err("Unable to collect tracking devices: invalid user buffer\n");
		return -ENODATA;
	}

	if (get.p_cbt_info == NULL) {
		res = tracking_collect(0x7FFFffff, NULL, &get.count);
		if (res == SUCCESS) {
			len = copy_to_user((void *)arg, (void *)&get,
					   sizeof(struct ioctl_tracking_collect_s));
			if (len != 0) {
				pr_err("Unable to collect tracking devices: invalid user buffer for arguments\n");
				res = -ENODATA;
			}
		} else {
			pr_err("Failed to execute tracking_collect. errno=%d\n", res);
		}
	} else {
		struct cbt_info_s *p_cbt_info = NULL;

		p_cbt_info = kcalloc(get.count, sizeof(struct cbt_info_s), GFP_KERNEL);
		if (p_cbt_info == NULL)
			return -ENOMEM;

		do {
			res = tracking_collect(get.count, p_cbt_info, &get.count);
			if (res != SUCCESS) {
				pr_err("Failed to execute tracking_collect. errno=%d\n", res);
				break;
			}
			len = copy_to_user(get.p_cbt_info, p_cbt_info,
					      get.count * sizeof(struct cbt_info_s));
			if (len != 0) {
				pr_err("Unable to collect tracking devices: invalid user buffer for CBT info\n");
				res = -ENODATA;
				break;
			}

			len = copy_to_user((void *)arg, (void *)&get,
					   sizeof(struct ioctl_tracking_collect_s));
			if (len != 0) {
				pr_err("Unable to collect tracking devices: invalid user buffer for arguments\n");
				res = -ENODATA;
				break;
			}

		} while (false);

		kfree(p_cbt_info);
		p_cbt_info = NULL;
	}
	return res;
}

int ioctl_tracking_block_size(unsigned long arg)
{
	unsigned long len;
	unsigned int blk_sz = change_tracking_block_size();

	len = copy_to_user((void *)arg, &blk_sz, sizeof(unsigned int));
	if (len != 0) {
		pr_err("Unable to get tracking block size: invalid user buffer for arguments\n");
		return -ENODATA;
	}
	return SUCCESS;
}

int ioctl_tracking_read_cbt_map(unsigned long arg)
{
	dev_t dev_id;
	unsigned long len;
	struct ioctl_tracking_read_cbt_bitmap_s readbitmap;

	len = copy_from_user(&readbitmap, (void *)arg,
				sizeof(struct ioctl_tracking_read_cbt_bitmap_s));
	if (len != 0) {
		pr_err("Unable to read CBT map: invalid user buffer\n");
		return -ENODATA;
	}

	dev_id = MKDEV(readbitmap.dev_id.major, readbitmap.dev_id.minor);
	return tracking_read_cbt_bitmap(dev_id, readbitmap.offset, readbitmap.length,
					(void *)readbitmap.buff);
}

int ioctl_tracking_mark_dirty_blocks(unsigned long arg)
{
	unsigned long len;
	struct ioctl_tracking_mark_dirty_blocks_s param;
	struct block_range_s *p_dirty_blocks;
	size_t buffer_size;
	int result = SUCCESS;

	len = copy_from_user(&param, (void *)arg,
			     sizeof(struct ioctl_tracking_mark_dirty_blocks_s));
	if (len != 0) {
		pr_err("Unable to mark dirty blocks: invalid user buffer\n");
		return -ENODATA;
	}

	buffer_size = param.count * sizeof(struct block_range_s);
	p_dirty_blocks = kzalloc(buffer_size, GFP_KERNEL);
	if (p_dirty_blocks == NULL) {
		pr_err("Unable to mark dirty blocks: cannot allocate [%ld] bytes\n", buffer_size);
		return -ENOMEM;
	}

	do {
		dev_t image_dev_id;

		len = copy_from_user(p_dirty_blocks, (void *)param.p_dirty_blocks, buffer_size);
		if (len != 0) {
			pr_err("Unable to mark dirty blocks: invalid user buffer\n");
			result = -ENODATA;
			break;
		}

		image_dev_id = MKDEV(param.image_dev_id.major, param.image_dev_id.minor);
		result = snapimage_mark_dirty_blocks(image_dev_id, p_dirty_blocks, param.count);
	} while (false);
	kfree(p_dirty_blocks);

	return result;
}

int ioctl_snapshot_create(unsigned long arg)
{
	unsigned long len;
	size_t dev_id_buffer_size;
	int status;
	struct ioctl_snapshot_create_s param;
	struct ioctl_dev_id_s *pk_dev_id = NULL;

	len = copy_from_user(&param, (void *)arg, sizeof(struct ioctl_snapshot_create_s));
	if (len != 0) {
		pr_err("Unable to create snapshot: invalid user buffer\n");
		return -ENODATA;
	}

	dev_id_buffer_size = sizeof(struct ioctl_dev_id_s) * param.count;
	pk_dev_id = kzalloc(dev_id_buffer_size, GFP_KERNEL);
	if (pk_dev_id == NULL) {
		pr_err("Unable to create snapshot: cannot allocate [%ld] bytes\n",
		       dev_id_buffer_size);
		return -ENOMEM;
	}

	do {
		size_t dev_buffer_size;
		dev_t *p_dev = NULL;
		int inx = 0;

		len = copy_from_user(pk_dev_id, (void *)param.p_dev_id,
				     param.count * sizeof(struct ioctl_dev_id_s));
		if (len != 0) {
			pr_err("Unable to create snapshot: invalid user buffer for parameters\n");
			status = -ENODATA;
			break;
		}

		dev_buffer_size = sizeof(dev_t) * param.count;
		p_dev = kzalloc(dev_buffer_size, GFP_KERNEL);
		if (p_dev == NULL) {
			pr_err("Unable to create snapshot: cannot allocate [%ld] bytes\n",
			       dev_buffer_size);
			status = -ENOMEM;
			break;
		}

		for (inx = 0; inx < param.count; ++inx)
			p_dev[inx] = MKDEV(pk_dev_id[inx].major, pk_dev_id[inx].minor);

		status = snapshot_create(p_dev, param.count, get_change_tracking_block_size_pow(),
					 &param.snapshot_id);

		kfree(p_dev);
		p_dev = NULL;

	} while (false);
	kfree(pk_dev_id);
	pk_dev_id = NULL;

	if (status == SUCCESS) {
		len = copy_to_user((void *)arg, &param, sizeof(struct ioctl_snapshot_create_s));
		if (len != 0) {
			pr_err("Unable to create snapshot: invalid user buffer\n");
			status = -ENODATA;
		}
	}

	return status;
}

int ioctl_snapshot_destroy(unsigned long arg)
{
	unsigned long len;
	unsigned long long param;

	len = copy_from_user(&param, (void *)arg, sizeof(unsigned long long));
	if (len != 0) {
		pr_err("Unable to destroy snapshot: invalid user buffer\n");
		return -ENODATA;
	}

	return snapshot_destroy(param);
}

static inline dev_t _snapstore_dev(struct ioctl_dev_id_s *dev_id)
{
	if ((dev_id->major == 0) && (dev_id->minor == 0))
		return 0; //memory snapstore

	if ((dev_id->major == -1) && (dev_id->minor == -1))
		return 0xFFFFffff; //multidevice snapstore

	return MKDEV(dev_id->major, dev_id->minor);
}

int ioctl_snapstore_create(unsigned long arg)
{
	unsigned long len;
	int res = SUCCESS;
	struct ioctl_snapstore_create_s param;
	size_t inx = 0;
	dev_t *dev_id_set = NULL;

	len = copy_from_user(&param, (void *)arg, sizeof(struct ioctl_snapstore_create_s));
	if (len != 0) {
		pr_err("Unable to create snapstore: invalid user buffer\n");
		return -EINVAL;
	}

	dev_id_set = kcalloc(param.count, sizeof(dev_t), GFP_KERNEL);
	if (dev_id_set == NULL)
		return -ENOMEM;

	for (inx = 0; inx < param.count; ++inx) {
		struct ioctl_dev_id_s dev_id;

		len = copy_from_user(&dev_id, param.p_dev_id + inx, sizeof(struct ioctl_dev_id_s));
		if (len != 0) {
			pr_err("Unable to create snapstore: ");
			pr_err("invalid user buffer for parameters\n");

			res = -ENODATA;
			break;
		}

		dev_id_set[inx] = MKDEV(dev_id.major, dev_id.minor);
	}

	if (res == SUCCESS)
		res = snapstore_create((uuid_t *)param.id, _snapstore_dev(&param.snapstore_dev_id),
				       dev_id_set, (size_t)param.count);

	kfree(dev_id_set);

	return res;
}

int ioctl_snapstore_file(unsigned long arg)
{
	unsigned long len;
	int res = SUCCESS;
	struct ioctl_snapstore_file_add_s param;
	struct big_buffer *ranges = NULL;
	size_t ranges_buffer_size;

	len = copy_from_user(&param, (void *)arg, sizeof(struct ioctl_snapstore_file_add_s));
	if (len != 0) {
		pr_err("Unable to add file to snapstore: invalid user buffer\n");
		return -EINVAL;
	}

	ranges_buffer_size = sizeof(struct ioctl_range_s) * param.range_count;

	ranges = big_buffer_alloc(ranges_buffer_size, GFP_KERNEL);
	if (ranges == NULL) {
		pr_err("Unable to add file to snapstore: cannot allocate [%ld] bytes\n",
		       ranges_buffer_size);
		return -ENOMEM;
	}

	if (big_buffer_copy_from_user((void *)param.ranges, 0, ranges, ranges_buffer_size)
		!= ranges_buffer_size) {

		pr_err("Unable to add file to snapstore: invalid user buffer for parameters\n");
		res = -ENODATA;
	} else
		res = snapstore_add_file((uuid_t *)(param.id), ranges, (size_t)param.range_count);

	big_buffer_free(ranges);

	return res;
}

int ioctl_snapstore_memory(unsigned long arg)
{
	unsigned long len;
	int res = SUCCESS;
	struct ioctl_snapstore_memory_limit_s param;

	len = copy_from_user(&param, (void *)arg, sizeof(struct ioctl_snapstore_memory_limit_s));
	if (len != 0) {
		pr_err("Unable to add memory block to snapstore: invalid user buffer\n");
		return -EINVAL;
	}

	res = snapstore_add_memory((uuid_t *)param.id, param.size);

	return res;
}
int ioctl_snapstore_cleanup(unsigned long arg)
{
	unsigned long len;
	int res = SUCCESS;
	struct ioctl_snapstore_cleanup_s param;

	len = copy_from_user(&param, (void *)arg, sizeof(struct ioctl_snapstore_cleanup_s));
	if (len != 0) {
		pr_err("Unable to perform snapstore cleanup: invalid user buffer\n");
		return -EINVAL;
	}

	pr_err("id=%pUB\n", (uuid_t *)param.id);
	res = snapstore_cleanup((uuid_t *)param.id, &param.filled_bytes);

	if (res == SUCCESS) {
		if (0 !=
		    copy_to_user((void *)arg, &param, sizeof(struct ioctl_snapstore_cleanup_s))) {
			pr_err("Unable to perform snapstore cleanup: invalid user buffer\n");
			res = -ENODATA;
		}
	}

	return res;
}

#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
int ioctl_snapstore_file_multidev(unsigned long arg)
{
	unsigned long len;
	int res = SUCCESS;
	struct ioctl_snapstore_file_add_multidev_s param;
	struct big_buffer *ranges = NULL; //struct ioctl_range_s* ranges = NULL;
	size_t ranges_buffer_size;

	len = copy_from_user(&param, (void *)arg,
				sizeof(struct ioctl_snapstore_file_add_multidev_s));
	if (len != 0) {
		pr_err("Unable to add file to multidev snapstore: invalid user buffer\n");
		return -EINVAL;
	}

	ranges_buffer_size = sizeof(struct ioctl_range_s) * param.range_count;

	ranges = big_buffer_alloc(ranges_buffer_size, GFP_KERNEL);
	if (ranges == NULL) {
		pr_err("Unable to add file to multidev snapstore: cannot allocate [%ld] bytes\n",
		       ranges_buffer_size);
		return -ENOMEM;
	}

	do {
		uuid_t *id = (uuid_t *)(param.id);
		dev_t snapstore_device = MKDEV(param.dev_id.major, param.dev_id.minor);
		size_t ranges_cnt = (size_t)param.range_count;

		if (ranges_buffer_size != big_buffer_copy_from_user((void *)param.ranges, 0, ranges,
								    ranges_buffer_size)) {
			pr_err("Unable to add file to snapstore: invalid user buffer for parameters\n");
			res = -ENODATA;
			break;
		}

		res = snapstore_add_multidev(id, snapstore_device, ranges, ranges_cnt);
	} while (false);
	big_buffer_free(ranges);

	return res;
}

#endif
//////////////////////////////////////////////////////////////////////////

/*
 * Snapshot get errno for device
 */
int ioctl_snapshot_errno(unsigned long arg)
{
	unsigned long len;
	int res;
	struct ioctl_snapshot_errno_s param;

	len = copy_from_user(&param, (void *)arg, sizeof(struct ioctl_dev_id_s));
	if (len != 0) {
		pr_err("Unable failed to get snapstore error code: invalid user buffer\n");
		return -EINVAL;
	}

	res = snapstore_device_errno(MKDEV(param.dev_id.major, param.dev_id.minor),
				     &param.err_code);

	if (res != SUCCESS)
		return res;

	len = copy_to_user((void *)arg, &param, sizeof(struct ioctl_snapshot_errno_s));
	if (len != 0) {
		pr_err("Unable to get snapstore error code: invalid user buffer\n");
		return -EINVAL;
	}

	return SUCCESS;
}

int ioctl_collect_snapimages(unsigned long arg)
{
	unsigned long len;
	int status = SUCCESS;
	struct ioctl_collect_shapshot_images_s param;

	len = copy_from_user(&param, (void *)arg, sizeof(struct ioctl_collect_shapshot_images_s));
	if (len != 0) {
		pr_err("Unable to collect snapshot images: invalid user buffer\n");
		return -ENODATA;
	}

	status = snapimage_collect_images(param.count, param.p_image_info, &param.count);

	len = copy_to_user((void *)arg, &param, sizeof(struct ioctl_collect_shapshot_images_s));
	if (len != 0) {
		pr_err("Unable to collect snapshot images: invalid user buffer\n");
		return -ENODATA;
	}

	return status;
}

struct blk_snap_ioctl_table {
	unsigned int cmd;
	int (*fn)(unsigned long arg);
};

static struct blk_snap_ioctl_table blk_snap_ioctl_table[] = {
	{ (IOCTL_COMPATIBILITY_FLAGS), ioctl_compatibility_flags },
	{ (IOCTL_GETVERSION), ioctl_get_version },

	{ (IOCTL_TRACKING_ADD), ioctl_tracking_add },
	{ (IOCTL_TRACKING_REMOVE), ioctl_tracking_remove },
	{ (IOCTL_TRACKING_COLLECT), ioctl_tracking_collect },
	{ (IOCTL_TRACKING_BLOCK_SIZE), ioctl_tracking_block_size },
	{ (IOCTL_TRACKING_READ_CBT_BITMAP), ioctl_tracking_read_cbt_map },
	{ (IOCTL_TRACKING_MARK_DIRTY_BLOCKS), ioctl_tracking_mark_dirty_blocks },

	{ (IOCTL_SNAPSHOT_CREATE), ioctl_snapshot_create },
	{ (IOCTL_SNAPSHOT_DESTROY), ioctl_snapshot_destroy },
	{ (IOCTL_SNAPSHOT_ERRNO), ioctl_snapshot_errno },

	{ (IOCTL_SNAPSTORE_CREATE), ioctl_snapstore_create },
	{ (IOCTL_SNAPSTORE_FILE), ioctl_snapstore_file },
	{ (IOCTL_SNAPSTORE_MEMORY), ioctl_snapstore_memory },
	{ (IOCTL_SNAPSTORE_CLEANUP), ioctl_snapstore_cleanup },
#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
	{ (IOCTL_SNAPSTORE_FILE_MULTIDEV), ioctl_snapstore_file_multidev },
#endif
	{ (IOCTL_COLLECT_SNAPSHOT_IMAGES), ioctl_collect_snapimages },
	{ 0, NULL }
};

long ctrl_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long status = -ENOTTY;
	size_t inx = 0;

	while (blk_snap_ioctl_table[inx].cmd != 0) {
		if (blk_snap_ioctl_table[inx].cmd == cmd) {
			status = blk_snap_ioctl_table[inx].fn(arg);
			break;
		}
		++inx;
	}

	return status;
}
