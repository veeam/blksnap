#pragma once
#include <linux/kref.h>
#include <linux/wait.h>
#include <linux/kfifo.h>

typedef struct ctrl_pipe_s
{
	struct list_head link;

	struct kref refcount; 

	wait_queue_head_t readq;

	struct kfifo cmd_to_user;
	struct spinlock cmd_to_user_lock;
}ctrl_pipe_t;

ctrl_pipe_t* ctrl_pipe_get_resource( ctrl_pipe_t* pipe );
void ctrl_pipe_put_resource( ctrl_pipe_t* pipe );

void ctrl_pipe_done( void );

ctrl_pipe_t* ctrl_pipe_new( void );


ssize_t ctrl_pipe_read( ctrl_pipe_t* pipe, char __user *buffer, size_t length );
ssize_t ctrl_pipe_write( ctrl_pipe_t* pipe, const char __user *buffer, size_t length );

unsigned int ctrl_pipe_poll( ctrl_pipe_t* pipe );


void ctrl_pipe_request_halffill( ctrl_pipe_t* pipe, unsigned long long filled_status );
void ctrl_pipe_request_overflow( ctrl_pipe_t* pipe, unsigned int error_code, unsigned long long filled_status );
void ctrl_pipe_request_terminate( ctrl_pipe_t* pipe, unsigned long long filled_status );
