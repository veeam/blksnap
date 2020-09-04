#include "stdafx.h"
#include <linux/poll.h>
#include "ctrl_pipe.h"
#include "version.h"
#include "blk-snap-ctl.h"
#include <linux/uuid.h>
#include "snapstore.h"

#define SECTION "ctrl_pipe "
#include "log_format.h"

typedef struct cmd_to_user_s
{
    content_t content;
    char* request_buffer;
    size_t request_size;//in bytes
}cmd_to_user_t;

ssize_t ctrl_pipe_command_initiate( ctrl_pipe_t* pipe, const char __user *buffer, size_t length );
ssize_t ctrl_pipe_command_next_portion( ctrl_pipe_t* pipe, const char __user *buffer, size_t length );
#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
ssize_t ctrl_pipe_command_next_portion_multidev( ctrl_pipe_t* pipe, const char __user *buffer, size_t length );
#endif

void ctrl_pipe_request_acknowledge( ctrl_pipe_t* pipe, unsigned int result );
void ctrl_pipe_request_invalid( ctrl_pipe_t* pipe );


container_t CtrlPipes;


void ctrl_pipe_init( void )
{
    log_tr( "Ctrl pipes initialization" );
    container_init( &CtrlPipes, sizeof( ctrl_pipe_t ) );
}

void ctrl_pipe_done( void )
{
    log_tr( "Ctrl pipes - done" );
    if (SUCCESS != container_done( &CtrlPipes )){
        log_err( "Unable to perform ctrl pipes cleanup: container is not empty" );
    };
}

void ctrl_pipe_release_cb( void* resource )
{
    ctrl_pipe_t* pipe = (ctrl_pipe_t*)resource;

    //log_tr( "Ctrl pipe release" );

    while (!container_empty( &pipe->cmd_to_user )){
        cmd_to_user_t* request = (cmd_to_user_t*)container_get_first( &pipe->cmd_to_user );
        kfree( request->request_buffer );

        content_free( &request->content );
    }

    if (SUCCESS != container_done( &pipe->cmd_to_user )){
        log_err( "Unable to perform pipe commands cleanup: container is not empty" );
    };

    container_free( &pipe->content );
}

ctrl_pipe_t* ctrl_pipe_new( void )
{
    ctrl_pipe_t* pipe;
    //log_tr( "Create new ctrl pipe" );

    if (NULL == (pipe = (ctrl_pipe_t*)container_new( &CtrlPipes ))){
        log_tr( "Failed to create new ctrl pipe: not enough memory" );
        return NULL;
    }

    container_init( &pipe->cmd_to_user, sizeof( cmd_to_user_t ) );
    shared_resource_init( &pipe->sharing_header, pipe, ctrl_pipe_release_cb );
    init_waitqueue_head( &pipe->readq );

    return pipe;
}

ssize_t ctrl_pipe_read( ctrl_pipe_t* pipe, char __user *buffer, size_t length )
{
    ssize_t processed = 0;
    cmd_to_user_t* cmd_to_user = NULL;

    if (container_empty( &pipe->cmd_to_user )){ //nothing to read
        if (wait_event_interruptible( pipe->readq, !container_empty( &pipe->cmd_to_user ) )){
            log_err( "Unable to wait for pipe read queue: interrupt signal was received " );
            return -ERESTARTSYS;
        }
    };

    cmd_to_user = (cmd_to_user_t*)container_get_first( &pipe->cmd_to_user );
    if (cmd_to_user == NULL){
        log_err( "Failed to read command from ctrl pipe" );
        return -ERESTARTSYS;
    }

    do {
        if (length < cmd_to_user->request_size){
            log_err_sz( "Unable to read command from ctrl pipe: user buffer is too small. Length requested =", cmd_to_user->request_size );
            processed = -ENODATA;
            break;
        }

        if (0 != copy_to_user( buffer, cmd_to_user->request_buffer, cmd_to_user->request_size )){
            log_err( "Unable to read command from ctrl pipe: invalid user buffer" );
            processed = -EINVAL;
            break;
        }

        processed = cmd_to_user->request_size;
    } while (false);


    if (processed > 0){
        kfree( cmd_to_user->request_buffer );
        content_free( &cmd_to_user->content );
    }
    else
        container_push_top( &pipe->cmd_to_user, &cmd_to_user->content ); //push to top of queue

    return processed;
}

ssize_t ctrl_pipe_write( ctrl_pipe_t* pipe, const char __user *buffer, size_t length )
{
    ssize_t processed = 0;

    do{
        unsigned int command;

        if ((length - processed) < 4){
            log_err_sz( "Unable to write command to ctrl pipe: invalid command length=", length);
            break;
        }
        if (0 != copy_from_user( &command, buffer + processed, sizeof(unsigned int) )){
            log_err( "Unable to write to pipe: invalid user buffer" );
            processed = -EINVAL;
            break;
        }
        processed += sizeof( unsigned int );
        //+4
        switch (command){
        case VEEAMSNAP_CHARCMD_INITIATE:
        {
            ssize_t res = ctrl_pipe_command_initiate( pipe, buffer + processed, length - processed );
            if (res >= 0)
                processed += res;
            else
                processed = res;
        }
            break;
        case VEEAMSNAP_CHARCMD_NEXT_PORTION:
        {
            ssize_t res = ctrl_pipe_command_next_portion( pipe, buffer + processed, length - processed );
            if (res >= 0)
                processed += res;
            else
                processed = res;
        }
            break;
#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
        case VEEAMSNAP_CHARCMD_NEXT_PORTION_MULTIDEV:
        {
            ssize_t res = ctrl_pipe_command_next_portion_multidev( pipe, buffer + processed, length - processed );
            if (res >= 0)
                processed += res;
            else
                processed = res;
        }
            break;
#endif
        default:
            log_err_format( "Ctrl pipe write error: invalid command [0x%x] received", command );
            break;
        }
    } while (false);
    return processed;
}

unsigned int ctrl_pipe_poll( ctrl_pipe_t* pipe )
{
    unsigned int mask = 0;

    if (!container_empty( &pipe->cmd_to_user )){
        mask |= (POLLIN | POLLRDNORM);     /* readable */
    }
    mask |= (POLLOUT | POLLWRNORM);   /* writable */

    return mask;
}


ssize_t ctrl_pipe_command_initiate( ctrl_pipe_t* pipe, const char __user *buffer, size_t length )
{
    int result = SUCCESS;
    ssize_t processed = 0;

    char* kernel_buffer = kmalloc( length, GFP_KERNEL );
    if (kernel_buffer == NULL){
        log_err_sz( "Unable to send next portion to pipe: cannot allocate buffer. length=", length );
        return -ENOMEM;
    }

    if (0 != copy_from_user( kernel_buffer, buffer, length )){
        kfree( kernel_buffer );
        log_err( "Unable to write to pipe: invalid user buffer" );
        return -EINVAL;
    }

    do{
        u64 stretch_empty_limit;
        unsigned int dev_id_list_length;
        uuid_t* unique_id;
        struct ioctl_dev_id_s* snapstore_dev_id;
        struct ioctl_dev_id_s* dev_id_list;

        //get snapstore uuid
        if ((length - processed) < 16){
            log_err_sz( "Unable to get snapstore uuid: invalid ctrl pipe initiate command. length=", length );
            break;
        }
        unique_id = (uuid_t*)(kernel_buffer + processed);
        processed += 16;
        //log_tr_uuid( "unique_id=", unique_id );


        //get snapstore empty limit
        if ((length - processed) < sizeof( u64 )){
            log_err_sz( "Unable to get stretch snapstore limit: invalid ctrl pipe initiate command. length=", length );
            break;
        }
        stretch_empty_limit = *(u64*)(kernel_buffer + processed);
        processed += sizeof( u64 );
        //log_tr_lld( "stretch_empty_limit=", stretch_empty_limit );


        //get snapstore device id
        if ((length - processed) < sizeof( struct ioctl_dev_id_s )){
            log_err_sz( "Unable to get snapstore device id: invalid ctrl pipe initiate command. length=", length );
            break;
        }
        snapstore_dev_id = (struct ioctl_dev_id_s*)(kernel_buffer + processed);
        processed += sizeof( struct ioctl_dev_id_s );
        //log_tr_dev_t( "snapstore_dev_id=", MKDEV( snapstore_dev_id->major, snapstore_dev_id->minor ) );


        //get device id list length
        if ((length - processed) < 4){
            log_err_sz( "Unable to get device id list length: ivalid ctrl pipe initiate command. length=", length );
            break;
        }
        dev_id_list_length = *(unsigned int*)(kernel_buffer + processed);
        processed += sizeof( unsigned int );
        //log_tr_d( "dev_id_list_length=", dev_id_list_length );


        //get devices id list
        if ((length - processed) < (dev_id_list_length*sizeof( struct ioctl_dev_id_s ))){
            log_err_sz( "Unable to get all devices from device id list: invalid ctrl pipe initiate command. length=", length );
            break;
        }
        dev_id_list = (struct ioctl_dev_id_s*)(kernel_buffer + processed);
        processed += (dev_id_list_length*sizeof( struct ioctl_dev_id_s ));

        //{
            //unsigned int dev_id_list_inx;
            //log_tr( "Initiate stretch snapstore for device:" )
            //for (dev_id_list_inx = 0; dev_id_list_inx < dev_id_list_length; ++dev_id_list_inx){
            //    log_tr_dev_id_s( "  ", dev_id_list[dev_id_list_inx] );
            //}
        //}

        {
            size_t inx;
            dev_t* dev_set;
            size_t dev_id_set_length = (size_t)dev_id_list_length;
            dev_t snapstore_dev;
            size_t dev_id_set_buffer_size;

            if ((snapstore_dev_id->major == -1) && (snapstore_dev_id->minor == -1))
                snapstore_dev = 0xFFFFffff; //multidevice
            else if ((snapstore_dev_id->major == 0) && (snapstore_dev_id->minor == 0))
                snapstore_dev = 0; //in memory
            else
                snapstore_dev = MKDEV( snapstore_dev_id->major, snapstore_dev_id->minor );

            dev_id_set_buffer_size = sizeof( dev_t ) * dev_id_set_length;
            dev_set = kzalloc( dev_id_set_buffer_size, GFP_KERNEL );
            if (NULL == dev_set){
                log_err( "Unable to process stretch snapstore initiation command: cannot allocate memory" );
                result = -ENOMEM;
                break;
            }

            for (inx = 0; inx < dev_id_set_length; ++inx)
                dev_set[inx] = MKDEV( dev_id_list[inx].major, dev_id_list[inx].minor );

            result = snapstore_create( unique_id, snapstore_dev, dev_set, dev_id_set_length );
            kfree( dev_set );
            if (result != SUCCESS){
                log_err_dev_t( "Failed to create snapstore on device ", snapstore_dev );
                break;
            }

            result = snapstore_stretch_initiate( unique_id, pipe, sector_from_streamsize( stretch_empty_limit ) );
            if (result != SUCCESS){
                log_err_uuid( "Failed to initiate stretch snapstore", unique_id );
                    break;
                }
            }
    } while (false);
    kfree( kernel_buffer );
    ctrl_pipe_request_acknowledge( pipe, result );
    
    if (result == SUCCESS)
        return processed;
    return result;
}

ssize_t ctrl_pipe_command_next_portion( ctrl_pipe_t* pipe, const char __user *buffer, size_t length )
{
    int result = SUCCESS;
    ssize_t processed = 0;
    page_array_t* ranges = NULL;

    do{
        uuid_t unique_id;
        unsigned int ranges_length;
        size_t ranges_buffer_size;

        //get snapstore id
        if ((length - processed) < 16){
            log_err_sz( "Unable to get snapstore id: invalid ctrl pipe next portion command. length=", length );
            break;
        }
        if (0 != copy_from_user(&unique_id, buffer + processed, sizeof(uuid_t))){
            log_err( "Unable to write to pipe: invalid user buffer" );
            processed = -EINVAL;
            break;
        }
        processed += 16;
        //log_tr_uuid( "snapstore unique_id=", unique_id );

        //get ranges length
        if ((length - processed) < 4){
            log_err_sz( "Unable to get device id list length: invalid ctrl pipe next portion command. length=", length );
            break;
        }
        if (0 != copy_from_user( &ranges_length, buffer + processed, sizeof( unsigned int ) )){
            log_err( "Unable to write to pipe: invalid user buffer" );
            processed = -EINVAL;
            break;
        }
        processed += sizeof( unsigned int );

        ranges_buffer_size = ranges_length*sizeof( struct ioctl_range_s );

        // ranges
        if ((length - processed) < (ranges_buffer_size)){
            log_err_sz( "Unable to get all ranges: invalid ctrl pipe next portion command. length=", length );
            break;
        }
        ranges = page_array_alloc( page_count_calc( ranges_buffer_size ), GFP_KERNEL );
        if (ranges == NULL){
            log_err( "Unable to allocate page array buffer: failed to process next portion command." );
            processed = -ENOMEM;
            break;
        }
        if (ranges_buffer_size != page_array_user2page( buffer + processed, 0, ranges, ranges_buffer_size )){
            log_err( "Unable to process next portion command: invalid user buffer for parameters." );
            processed = -EINVAL;
            break;
        }
        processed += ranges_buffer_size;

        {
            result = snapstore_add_file( &unique_id, ranges, ranges_length );

            if (result != SUCCESS){
                log_err( "Failed to add file to snapstore" );
                result = -ENODEV;
                break;
            }
        }
    } while (false);
    if (ranges)
        page_array_free( ranges );

    if (result == SUCCESS)
        //log_traceln_sz( "processed=", processed );
        return processed;
    return result;
}
#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
ssize_t ctrl_pipe_command_next_portion_multidev( ctrl_pipe_t* pipe, const char __user *buffer, size_t length )
        {
    int result = SUCCESS;
    ssize_t processed = 0;
    page_array_t* ranges = NULL;

    do{
        uuid_t unique_id;
        int snapstore_major;
        int snapstore_minor;
        unsigned int ranges_length;
        size_t ranges_buffer_size;

        //get snapstore id
        if ((length - processed) < 16){
            log_err_sz( "Unable to get snapstore id: invalid ctrl pipe next portion command. length=", length );
            break;
        }
        if (0 != copy_from_user(&unique_id, buffer + processed, sizeof(uuid_t))){
            log_err( "Unable to write to pipe: invalid user buffer" );
            processed = -EINVAL;
            break;
        }
        processed += 16;
        //log_tr_uuid( "snapstore unique_id=", unique_id );

        //get device id
        if ((length - processed) < 8){
            log_err_sz( "Unable to get device id list length: invalid ctrl pipe next portion command. length=", length );
            break;
        }
        if (0 != copy_from_user( &snapstore_major, buffer + processed, sizeof( unsigned int ) )){
            log_err( "Unable to write to pipe: invalid user buffer" );
            processed = -EINVAL;
            break;
        }
        processed += sizeof( unsigned int );

        if (0 != copy_from_user( &snapstore_minor, buffer + processed, sizeof( unsigned int ) )){
            log_err( "Unable to write to pipe: invalid user buffer" );
            processed = -EINVAL;
            break;
        }
        processed += sizeof( unsigned int );

        //get ranges length
        if ((length - processed) < 4){
            log_err_sz( "Unable to get device id list length: invalid ctrl pipe next portion command. length=", length );
            break;
        }
        if (0 != copy_from_user( &ranges_length, buffer + processed, sizeof( unsigned int ) )){
            log_err( "Unable to write to pipe: invalid user buffer" );
            processed = -EINVAL;
                break;
            }
        processed += sizeof( unsigned int );

        ranges_buffer_size = ranges_length*sizeof( struct ioctl_range_s );

        // ranges
        if ((length - processed) < (ranges_buffer_size)){
            log_err_sz( "Unable to get all ranges: invalid ctrl pipe next portion command.  length=", length );
            break;
        }
        ranges = page_array_alloc( page_count_calc( ranges_buffer_size ), GFP_KERNEL );
        if (ranges == NULL){
            log_err( "Unable to process next portion command: failed to allocate page array buffer" );
            processed = -ENOMEM;
            break;
        }
        if (ranges_buffer_size != page_array_user2page( buffer + processed, 0, ranges, ranges_buffer_size )){
            log_err( "Unable to process next portion command: invalid user buffer from parameters." );
            processed = -EINVAL;
            break;
        }
        processed += ranges_buffer_size;

            {
            result = snapstore_add_multidev( &unique_id, MKDEV( snapstore_major, snapstore_minor ), ranges, ranges_length );

            if (result != SUCCESS){
                log_err( "Failed to add file to snapstore" );
                result = -ENODEV;
                break;
            }
        }
    } while (false);
    if (ranges)
        page_array_free( ranges );

    if (result == SUCCESS)
        //log_traceln_sz( "processed=", processed );
        return processed;
    return result;
}
#endif
void ctrl_pipe_push_request( ctrl_pipe_t* pipe, unsigned int* cmd, size_t cmd_len )
{
    cmd_to_user_t* request = NULL;

    request = (cmd_to_user_t*)content_new( &pipe->cmd_to_user );
    if (request == NULL){
        log_err( "Failed to create acknowledge command." );
        kfree( cmd );
        return;
    }

    request->request_size = cmd_len * sizeof( unsigned int );
    request->request_buffer = (char*)cmd;
    container_push_back( &pipe->cmd_to_user, &request->content );

    wake_up( &pipe->readq );
}

void ctrl_pipe_request_acknowledge( ctrl_pipe_t* pipe, unsigned int result )
{
    unsigned int* cmd = NULL;
    size_t cmd_len = 2;

    cmd = (unsigned int*)kmalloc( cmd_len * sizeof( unsigned int ), GFP_KERNEL );
    if (NULL == cmd){
        log_err( "Unable to create acknowledge command data: not enough memory" );
        return;
    }

    cmd[0] = VEEAMSNAP_CHARCMD_ACKNOWLEDGE;
    cmd[1] = result;

    ctrl_pipe_push_request( pipe, cmd, cmd_len );
}

void ctrl_pipe_request_halffill( ctrl_pipe_t* pipe, unsigned long long filled_status )
{
    unsigned int* cmd = NULL;
    size_t cmd_len = 3;

    log_tr( "Snapstore is half-full" );

    cmd = (unsigned int*)kmalloc( cmd_len * sizeof( unsigned int ), GFP_KERNEL );
    if (NULL == cmd){
        log_err( "Unable to create acknowledge command data: not enough memory" );
        return;
    }

    cmd[0] = (unsigned int)VEEAMSNAP_CHARCMD_HALFFILL;
    cmd[1] = (unsigned int)(filled_status & 0xFFFFffff); //lo
    cmd[2] = (unsigned int)(filled_status >> 32);

    ctrl_pipe_push_request( pipe, cmd, cmd_len );
}

void ctrl_pipe_request_overflow( ctrl_pipe_t* pipe, unsigned int error_code, unsigned long long filled_status )
{
    unsigned int* cmd = NULL;
    size_t cmd_len = 4;

    log_tr( "Snapstore overflow" );

    cmd = (unsigned int*)kmalloc( cmd_len * sizeof( unsigned int ), GFP_KERNEL );
    if (NULL == cmd){
        log_err( "Unable to create acknowledge command data: not enough memory" );
        return;
    }

    cmd[0] = (unsigned int)VEEAMSNAP_CHARCMD_OVERFLOW;
    cmd[1] = error_code;
    cmd[2] = (unsigned int)(filled_status & 0xFFFFffff); //lo
    cmd[3] = (unsigned int)(filled_status >> 32);

    ctrl_pipe_push_request( pipe, cmd, cmd_len );
}

void ctrl_pipe_request_terminate( ctrl_pipe_t* pipe, unsigned long long filled_status )
{
    unsigned int* cmd = NULL;
    size_t cmd_len = 3;

    log_tr( "Snapstore termination" );

    cmd = (unsigned int*)kmalloc( cmd_len * sizeof( unsigned int ), GFP_KERNEL );
    if (NULL == cmd){
        log_err( "Unable to create acknowledge command data: not enough memory" );
        return;
    }

    cmd[0] = (unsigned int)VEEAMSNAP_CHARCMD_TERMINATE;
    cmd[1] = (unsigned int)(filled_status & 0xFFFFffff); //lo
    cmd[2] = (unsigned int)(filled_status >> 32);

    ctrl_pipe_push_request( pipe, cmd, cmd_len );

}

void ctrl_pipe_request_invalid( ctrl_pipe_t* pipe )
{
    unsigned int* cmd = NULL;
    size_t cmd_len = 1;

    log_tr( "Ctrl pipe received invalid command" );

    cmd = (unsigned int*)kmalloc( cmd_len * sizeof( unsigned int ), GFP_KERNEL );
    if (NULL == cmd){
        log_err( "Unable to create acknowledge command data: not enough memory" );
        return;
    }

    cmd[0] = (unsigned int)VEEAMSNAP_CHARCMD_INVALID;

    ctrl_pipe_push_request( pipe, cmd, cmd_len );
}


