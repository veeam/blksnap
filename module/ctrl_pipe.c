// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-ctrl"
#include "common.h"
#include "ctrl_pipe.h"
#include "version.h"
#include "blk-snap-ctl.h"
#include "snapstore.h"
#include "big_buffer.h"

#include <linux/poll.h>
#include <linux/uuid.h>

#define CMD_TO_USER_FIFO_SIZE 1024

LIST_HEAD(ctl_pipes);
DECLARE_RWSEM(ctl_pipes_lock);


static void ctrl_pipe_push_request(struct ctrl_pipe *pipe, unsigned int *cmd, size_t cmd_len)
{
	kfifo_in_spinlocked(&pipe->cmd_to_user, cmd, (cmd_len * sizeof(unsigned int)),
			    &pipe->cmd_to_user_lock);

	wake_up(&pipe->readq);
}

static void ctrl_pipe_request_acknowledge(struct ctrl_pipe *pipe, unsigned int result)
{
	unsigned int cmd[2];

	cmd[0] = BLK_SNAP_CHARCMD_ACKNOWLEDGE;
	cmd[1] = result;

	ctrl_pipe_push_request(pipe, cmd, 2);
}

static inline dev_t _snapstore_dev(struct ioctl_dev_id_s *dev_id)
{
	if ((dev_id->major == 0) && (dev_id->minor == 0))
		return 0; //memory snapstore

	if ((dev_id->major == -1) && (dev_id->minor == -1))
		return 0xFFFFffff; //multidevice snapstore

	return MKDEV(dev_id->major, dev_id->minor);
}

static ssize_t ctrl_pipe_command_initiate(struct ctrl_pipe *pipe, const char __user *buffer,
					  size_t length)
{
	unsigned long len;
	int result = SUCCESS;
	ssize_t processed = 0;
	char *kernel_buffer;

	kernel_buffer = kmalloc(length, GFP_KERNEL);
	if (kernel_buffer == NULL)
		return -ENOMEM;

	len = copy_from_user(kernel_buffer, buffer, length);
	if (len != 0) {
		kfree(kernel_buffer);
		pr_err("Unable to write to pipe: invalid user buffer\n");
		return -EINVAL;
	}

	do {
		u64 stretch_empty_limit;
		unsigned int dev_id_list_length;
		uuid_t *unique_id;
		struct ioctl_dev_id_s *snapstore_dev_id;
		struct ioctl_dev_id_s *dev_id_list;

		//get snapstore uuid
		if ((length - processed) < 16) {
			pr_err("Unable to get snapstore uuid: invalid ctrl pipe initiate command. length=%lu\n",
			       length);
			break;
		}
		unique_id = (uuid_t *)(kernel_buffer + processed);
		processed += 16;

		//get snapstore empty limit
		if ((length - processed) < sizeof(u64)) {
			pr_err("Unable to get stretch snapstore limit: invalid ctrl pipe initiate command. length=%lu\n",
			       length);
			break;
		}
		stretch_empty_limit = *(u64 *)(kernel_buffer + processed);
		processed += sizeof(u64);

		//get snapstore device id
		if ((length - processed) < sizeof(struct ioctl_dev_id_s)) {
			pr_err("Unable to get snapstore device id: invalid ctrl pipe initiate command. length=%lu\n",
			       length);
			break;
		}
		snapstore_dev_id = (struct ioctl_dev_id_s *)(kernel_buffer + processed);
		processed += sizeof(struct ioctl_dev_id_s);

		//get device id list length
		if ((length - processed) < 4) {
			pr_err("Unable to get device id list length: ivalid ctrl pipe initiate command. length=%lu\n",
			       length);
			break;
		}
		dev_id_list_length = *(unsigned int *)(kernel_buffer + processed);
		processed += sizeof(unsigned int);

		//get devices id list
		if ((length - processed) < (dev_id_list_length * sizeof(struct ioctl_dev_id_s))) {
			pr_err("Unable to get all devices from device id list: invalid ctrl pipe initiate command. length=%lu\n",
			       length);
			break;
		}
		dev_id_list = (struct ioctl_dev_id_s *)(kernel_buffer + processed);
		processed += (dev_id_list_length * sizeof(struct ioctl_dev_id_s));

		{
			size_t inx;
			dev_t *dev_set;
			size_t dev_id_set_length = (size_t)dev_id_list_length;

			dev_set = kcalloc(dev_id_set_length, sizeof(dev_t), GFP_KERNEL);
			if (dev_set == NULL) {
				result = -ENOMEM;
				break;
			}

			for (inx = 0; inx < dev_id_set_length; ++inx)
				dev_set[inx] =
					MKDEV(dev_id_list[inx].major, dev_id_list[inx].minor);

			result = snapstore_create(unique_id, _snapstore_dev(snapstore_dev_id),
						  dev_set, dev_id_set_length);
			kfree(dev_set);
			if (result != SUCCESS) {
				pr_err("Failed to create snapstore\n");
				break;
			}

			result = snapstore_stretch_initiate(
				unique_id, pipe, (sector_t)to_sectors(stretch_empty_limit));
			if (result != SUCCESS) {
				pr_err("Failed to initiate stretch snapstore %pUB\n", unique_id);
				break;
			}
		}
	} while (false);
	kfree(kernel_buffer);
	ctrl_pipe_request_acknowledge(pipe, result);

	if (result == SUCCESS)
		return processed;
	return result;
}

static ssize_t ctrl_pipe_command_next_portion(struct ctrl_pipe *pipe, const char __user *buffer,
					      size_t length)
{
	unsigned long len;
	int result = SUCCESS;
	ssize_t processed = 0;
	struct big_buffer *ranges = NULL;

	do {
		uuid_t unique_id;
		unsigned int ranges_length;
		size_t ranges_buffer_size;

		//get snapstore id
		if ((length - processed) < 16) {
			pr_err("Unable to get snapstore id: ");
			pr_err("invalid ctrl pipe next portion command. length=%lu\n",
			       length);
			break;
		}
		len = copy_from_user(&unique_id, buffer + processed, sizeof(uuid_t));
		if (len != 0) {
			pr_err("Unable to write to pipe: invalid user buffer\n");
			processed = -EINVAL;
			break;
		}
		processed += 16;

		//get ranges length
		if ((length - processed) < 4) {
			pr_err("Unable to get device id list length: ");
			pr_err("invalid ctrl pipe next portion command. length=%lu\n",
			       length);
			break;
		}
		len = copy_from_user(&ranges_length, buffer + processed, sizeof(unsigned int));
		if (len != 0) {
			pr_err("Unable to write to pipe: invalid user buffer\n");
			processed = -EINVAL;
			break;
		}
		processed += sizeof(unsigned int);

		ranges_buffer_size = ranges_length * sizeof(struct ioctl_range_s);

		// ranges
		if ((length - processed) < (ranges_buffer_size)) {
			pr_err("Unable to get all ranges: ");
			pr_err("invalid ctrl pipe next portion command. length=%lu\n",
			       length);
			break;
		}
		ranges = big_buffer_alloc(ranges_buffer_size, GFP_KERNEL);
		if (ranges == NULL) {
			pr_err("Unable to allocate page array buffer: ");
			pr_err("failed to process next portion command\n");
			processed = -ENOMEM;
			break;
		}
		if (ranges_buffer_size !=
		    big_buffer_copy_from_user(buffer + processed, 0, ranges, ranges_buffer_size)) {
			pr_err("Unable to process next portion command: ");
			pr_err("invalid user buffer for parameters\n");
			processed = -EINVAL;
			break;
		}
		processed += ranges_buffer_size;

		{
			result = snapstore_add_file(&unique_id, ranges, ranges_length);

			if (result != SUCCESS) {
				pr_err("Failed to add file to snapstore\n");
				result = -ENODEV;
				break;
			}
		}
	} while (false);
	if (ranges)
		big_buffer_free(ranges);

	if (result == SUCCESS)
		return processed;
	return result;
}

#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
static ssize_t ctrl_pipe_command_next_portion_multidev(struct ctrl_pipe *pipe,
						       const char __user *buffer, size_t length)
{
	unsigned long len;
	int result = SUCCESS;
	ssize_t processed = 0;
	struct big_buffer *ranges = NULL;

	do {
		uuid_t unique_id;
		int snapstore_major;
		int snapstore_minor;
		unsigned int ranges_length;
		size_t ranges_buffer_size;

		//get snapstore id
		if ((length - processed) < 16) {
			pr_err("Unable to get snapstore id: ");
			pr_err("invalid ctrl pipe next portion command. length=%lu\n",
			       length);
			break;
		}
		len = copy_from_user(&unique_id, buffer + processed, sizeof(uuid_t));
		if (len != 0) {
			pr_err("Unable to write to pipe: invalid user buffer\n");
			processed = -EINVAL;
			break;
		}
		processed += 16;

		//get device id
		if ((length - processed) < 8) {
			pr_err("Unable to get device id list length: ");
			pr_err("invalid ctrl pipe next portion command. length=%lu\n", length);
			break;
		}
		len = copy_from_user(&snapstore_major, buffer + processed, sizeof(unsigned int));
		if (len != 0) {
			pr_err("Unable to write to pipe: invalid user buffer\n");
			processed = -EINVAL;
			break;
		}
		processed += sizeof(unsigned int);

		len = copy_from_user(&snapstore_minor, buffer + processed, sizeof(unsigned int));
		if (len != 0) {
			pr_err("Unable to write to pipe: invalid user buffer\n");
			processed = -EINVAL;
			break;
		}
		processed += sizeof(unsigned int);

		//get ranges length
		if ((length - processed) < 4) {
			pr_err("Unable to get device id list length: ");
			pr_err("invalid ctrl pipe next portion command. length=%lu\n",
			       length);
			break;
		}
		len = copy_from_user(&ranges_length, buffer + processed, sizeof(unsigned int));
		if (len != 0) {
			pr_err("Unable to write to pipe: invalid user buffer\n");
			processed = -EINVAL;
			break;
		}
		processed += sizeof(unsigned int);

		ranges_buffer_size = ranges_length * sizeof(struct ioctl_range_s);

		// ranges
		if ((length - processed) < (ranges_buffer_size)) {
			pr_err("Unable to get all ranges: ");
			pr_err("invalid ctrl pipe next portion command.  length=%lu\n",
			       length);
			break;
		}
		ranges = big_buffer_alloc(ranges_buffer_size, GFP_KERNEL);
		if (ranges == NULL) {
			pr_err("Unable to process next portion command: ");
			pr_err("failed to allocate page array buffer\n");
			processed = -ENOMEM;
			break;
		}
		if (ranges_buffer_size !=
		    big_buffer_copy_from_user(buffer + processed, 0, ranges, ranges_buffer_size)) {
			pr_err("Unable to process next portion command: ");
			pr_err("invalid user buffer from parameters\n");
			processed = -EINVAL;
			break;
		}
		processed += ranges_buffer_size;

		{
			result = snapstore_add_multidev(&unique_id,
							MKDEV(snapstore_major, snapstore_minor),
							ranges, ranges_length);

			if (result != SUCCESS) {
				pr_err("Failed to add file to snapstore\n");
				result = -ENODEV;
				break;
			}
		}
	} while (false);
	if (ranges)
		big_buffer_free(ranges);

	if (result == SUCCESS)
		return processed;

	return result;
}
#endif

static void ctrl_pipe_release_cb(struct kref *kref)
{
	struct ctrl_pipe *pipe = container_of(kref, struct ctrl_pipe, refcount);

	down_write(&ctl_pipes_lock);
	list_del(&pipe->link);
	up_write(&ctl_pipes_lock);

	kfifo_free(&pipe->cmd_to_user);

	kfree(pipe);
}

struct ctrl_pipe *ctrl_pipe_get_resource(struct ctrl_pipe *pipe)
{
	if (pipe)
		kref_get(&pipe->refcount);

	return pipe;
}

void ctrl_pipe_put_resource(struct ctrl_pipe *pipe)
{
	if (pipe)
		kref_put(&pipe->refcount, ctrl_pipe_release_cb);
}

void ctrl_pipe_done(void)
{
	bool is_empty;

	pr_err("Ctrl pipes - done\n");

	down_write(&ctl_pipes_lock);
	is_empty = list_empty(&ctl_pipes);
	up_write(&ctl_pipes_lock);

	if (!is_empty)
		pr_err("Unable to perform ctrl pipes cleanup: container is not empty\n");
}

struct ctrl_pipe *ctrl_pipe_new(void)
{
	int ret;
	struct ctrl_pipe *pipe;

	pipe = kzalloc(sizeof(struct ctrl_pipe), GFP_KERNEL);
	if (pipe == NULL)
		return NULL;

	INIT_LIST_HEAD(&pipe->link);

	ret = kfifo_alloc(&pipe->cmd_to_user, CMD_TO_USER_FIFO_SIZE, GFP_KERNEL);
	if (ret) {
		pr_err("Failed to allocate fifo. errno=%d.\n", ret);
		kfree(pipe);
		return NULL;
	}
	spin_lock_init(&pipe->cmd_to_user_lock);

	kref_init(&pipe->refcount);

	init_waitqueue_head(&pipe->readq);

	down_write(&ctl_pipes_lock);
	list_add_tail(&pipe->link, &ctl_pipes);
	up_write(&ctl_pipes_lock);

	return pipe;
}

ssize_t ctrl_pipe_read(struct ctrl_pipe *pipe, char __user *buffer, size_t length)
{
	int ret;
	unsigned int processed = 0;

	if (kfifo_is_empty_spinlocked(&pipe->cmd_to_user, &pipe->cmd_to_user_lock)) {
		//nothing to read
		ret = wait_event_interruptible(pipe->readq,
					       !kfifo_is_empty_spinlocked(&pipe->cmd_to_user,
									&pipe->cmd_to_user_lock));
		if (ret) {
			pr_err("Unable to wait for pipe read queue: interrupt signal was received\n");
			return -ERESTARTSYS;
		}
	}

	ret = kfifo_to_user(&pipe->cmd_to_user, buffer, length, &processed);
	if (ret) {
		pr_err("Failed to read command from ctrl pipe\n");
		return ret;
	}

	return (ssize_t)processed;
}

ssize_t ctrl_pipe_write(struct ctrl_pipe *pipe, const char __user *buffer, size_t length)
{
	ssize_t processed = 0;

	do {
		unsigned long len;
		unsigned int command;

		if ((length - processed) < 4) {
			pr_err("Unable to write command to ctrl pipe: invalid command length=%lu\n",
			       length);
			break;
		}
		len = copy_from_user(&command, buffer + processed, sizeof(unsigned int));
		if (len != 0) {
			pr_err("Unable to write to pipe: invalid user buffer\n");
			processed = -EINVAL;
			break;
		}
		processed += sizeof(unsigned int);
		//+4
		switch (command) {
		case BLK_SNAP_CHARCMD_INITIATE: {
			ssize_t res = ctrl_pipe_command_initiate(pipe, buffer + processed,
								 length - processed);
			if (res >= 0)
				processed += res;
			else
				processed = res;
		} break;
		case BLK_SNAP_CHARCMD_NEXT_PORTION: {
			ssize_t res = ctrl_pipe_command_next_portion(pipe, buffer + processed,
								     length - processed);
			if (res >= 0)
				processed += res;
			else
				processed = res;
		} break;
#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
		case BLK_SNAP_CHARCMD_NEXT_PORTION_MULTIDEV: {
			ssize_t res = ctrl_pipe_command_next_portion_multidev(
				pipe, buffer + processed, length - processed);
			if (res >= 0)
				processed += res;
			else
				processed = res;
		} break;
#endif
		default:
			pr_err("Ctrl pipe write error: invalid command [0x%x] received\n", command);
			break;
		}
	} while (false);
	return processed;
}

unsigned int ctrl_pipe_poll(struct ctrl_pipe *pipe)
{
	unsigned int mask = 0;

	if (!kfifo_is_empty_spinlocked(&pipe->cmd_to_user, &pipe->cmd_to_user_lock))
		mask |= (POLLIN | POLLRDNORM); /* readable */

	mask |= (POLLOUT | POLLWRNORM); /* writable */

	return mask;
}

void ctrl_pipe_request_halffill(struct ctrl_pipe *pipe, unsigned long long filled_status)
{
	unsigned int cmd[3];

	pr_err("Snapstore is half-full\n");

	cmd[0] = (unsigned int)BLK_SNAP_CHARCMD_HALFFILL;
	cmd[1] = (unsigned int)(filled_status & 0xFFFFffff); //lo
	cmd[2] = (unsigned int)(filled_status >> 32);

	ctrl_pipe_push_request(pipe, cmd, 3);
}

void ctrl_pipe_request_overflow(struct ctrl_pipe *pipe, unsigned int error_code,
				unsigned long long filled_status)
{
	unsigned int cmd[4];

	pr_err("Snapstore overflow\n");

	cmd[0] = (unsigned int)BLK_SNAP_CHARCMD_OVERFLOW;
	cmd[1] = error_code;
	cmd[2] = (unsigned int)(filled_status & 0xFFFFffff); //lo
	cmd[3] = (unsigned int)(filled_status >> 32);

	ctrl_pipe_push_request(pipe, cmd, 4);
}

void ctrl_pipe_request_terminate(struct ctrl_pipe *pipe, unsigned long long filled_status)
{
	unsigned int cmd[3];

	pr_err("Snapstore termination\n");

	cmd[0] = (unsigned int)BLK_SNAP_CHARCMD_TERMINATE;
	cmd[1] = (unsigned int)(filled_status & 0xFFFFffff); //lo
	cmd[2] = (unsigned int)(filled_status >> 32);

	ctrl_pipe_push_request(pipe, cmd, 3);
}
