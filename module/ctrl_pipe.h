/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <linux/kref.h>
#include <linux/wait.h>
#include <linux/kfifo.h>

struct ctrl_pipe {
	struct list_head link;

	struct kref refcount;

	wait_queue_head_t readq;

	struct kfifo cmd_to_user;
	struct spinlock cmd_to_user_lock;
};

struct ctrl_pipe *ctrl_pipe_get_resource(struct ctrl_pipe *pipe);
void ctrl_pipe_put_resource(struct ctrl_pipe *pipe);

void ctrl_pipe_done(void);

struct ctrl_pipe *ctrl_pipe_new(void);

ssize_t ctrl_pipe_read(struct ctrl_pipe *pipe, char __user *buffer, size_t length);
ssize_t ctrl_pipe_write(struct ctrl_pipe *pipe, const char __user *buffer, size_t length);

unsigned int ctrl_pipe_poll(struct ctrl_pipe *pipe);

void ctrl_pipe_request_halffill(struct ctrl_pipe *pipe, unsigned long long filled_status);
void ctrl_pipe_request_overflow(struct ctrl_pipe *pipe, unsigned int error_code,
				unsigned long long filled_status);
void ctrl_pipe_request_terminate(struct ctrl_pipe *pipe, unsigned long long filled_status);
