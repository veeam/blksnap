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

#include <linux/module.h>
#include <linux/poll.h>
#include <linux/uaccess.h>

int get_change_tracking_block_size_pow(void);

static atomic_t g_dev_open_cnt = ATOMIC_INIT( 0 );

static struct ioctl_getversion_s version = {
	.major		= FILEVER_MAJOR,
	.minor		= FILEVER_MINOR,
	.revision	= FILEVER_REVISION,
	.build		= 0
};


void ctrl_done( void )
{
	ctrl_pipe_done( );
}

ssize_t ctrl_read(struct file *fl, char __user *buffer, size_t length, loff_t *offset)
{
	ssize_t bytes_read = 0;
	ctrl_pipe_t* pipe = (ctrl_pipe_t*)fl->private_data;

	bytes_read = ctrl_pipe_read( pipe, buffer, length );
	if (bytes_read == 0)
		if (fl->f_flags & O_NONBLOCK)
			bytes_read = -EAGAIN;

	return bytes_read;
}


ssize_t ctrl_write( struct file *fl, const char __user *buffer, size_t length, loff_t *offset )
{
	ssize_t bytes_wrote = 0;

	ctrl_pipe_t* pipe = (ctrl_pipe_t*)fl->private_data;
	if (NULL == pipe){
		pr_err( "Unable to write into pipe: invalid pipe pointer\n" );
		bytes_wrote = -EINVAL;
	}

	bytes_wrote = ctrl_pipe_write( pipe, buffer, length );
	return bytes_wrote;
}


unsigned int ctrl_poll( struct file *fl, struct poll_table_struct *wait )
{
	ctrl_pipe_t* pipe = (ctrl_pipe_t*)fl->private_data;

	return ctrl_pipe_poll( pipe );
}


int ctrl_open(struct inode *inode, struct file *fl)
{
	fl->f_pos = 0;

	if(false == try_module_get( THIS_MODULE ))
		return -EINVAL;

	fl->private_data = (void*)ctrl_pipe_new( );
	if (fl->private_data == NULL){
		pr_err( "Failed to open ctrl file\n" );
		return -ENOMEM;
	}

	atomic_inc( &g_dev_open_cnt );

	return SUCCESS;
}


int ctrl_release(struct inode *inode, struct file *fl)
{
	int result = SUCCESS;

	if ( atomic_read( &g_dev_open_cnt ) > 0 ){
		module_put( THIS_MODULE );
		ctrl_pipe_put_resource( (ctrl_pipe_t*)fl->private_data );

		atomic_dec( &g_dev_open_cnt );
	}
	else{
		pr_err( "Unable to close ctrl file: the file is already closed\n" );
		result = -EALREADY;
	}

	return result;
}


int ioctl_compatibility_flags( unsigned long arg )
{
	struct ioctl_compatibility_flags_s param;

	param.flags = 0;
	param.flags |= VEEAMSNAP_COMPATIBILITY_SNAPSTORE;
#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
	param.flags |= VEEAMSNAP_COMPATIBILITY_MULTIDEV;
#endif

	if (0 != copy_to_user( (void*)arg, &param, sizeof( struct ioctl_compatibility_flags_s ) )){
		pr_err( "Unable to get compatibility flags: invalid user buffer\n" );
		return -EINVAL;
	}

	return SUCCESS;
}

int ioctl_get_version( unsigned long arg )
{
	pr_info( "Get version\n" );

	if (0 != copy_to_user( (void*)arg, &version, sizeof( struct ioctl_getversion_s ) )){
		pr_err( "Unable to get version: invalid user buffer\n" );
		return -ENODATA;
	}

	return SUCCESS;
}

int ioctl_tracking_add( unsigned long arg )
{
	struct ioctl_dev_id_s dev;

	if (0 != copy_from_user( &dev, (void*)arg, sizeof( struct ioctl_dev_id_s ) )){
		pr_err( "Unable to add device under tracking: invalid user buffer\n" );
		return -ENODATA;
	}

	return tracking_add( MKDEV( dev.major, dev.minor ), get_change_tracking_block_size_pow(), 0ull );
}

int ioctl_tracking_remove( unsigned long arg )
{
	struct ioctl_dev_id_s dev;

	if (0 != copy_from_user( &dev, (void*)arg, sizeof( struct ioctl_dev_id_s ) )){
		pr_err( "Unable to remove device from tracking: invalid user buffer\n" );
		return -ENODATA;
	}
	return tracking_remove( MKDEV( dev.major, dev.minor ) );;
}

int ioctl_tracking_collect( unsigned long arg )
{
	int res;
	struct ioctl_tracking_collect_s get;

	pr_info( "Collecting tracking devices:\n" );

	if (0 != copy_from_user( &get, (void*)arg, sizeof( struct ioctl_tracking_collect_s ) )){
		pr_err( "Unable to collect tracking devices: invalid user buffer\n" );
		return -ENODATA;
	}

	if (get.p_cbt_info == NULL){
		res = tracking_collect(0x7FFFffff, NULL, &get.count);
		if (SUCCESS == res){
			if (0 != copy_to_user((void*)arg, (void*)&get, sizeof(struct ioctl_tracking_collect_s))){
				pr_err("Unable to collect tracking devices: invalid user buffer for arguments\n");
				res = -ENODATA;
			}
		}
		else{
			pr_err("Failed to execute tracking_collect. errno=%d\n", res);
		}
	}
	else
	{
		struct cbt_info_s* p_cbt_info = NULL;

		p_cbt_info = kzalloc(get.count * sizeof(struct cbt_info_s), GFP_KERNEL);
		if (NULL == p_cbt_info){
			pr_err("Unable to collect tracing devices: cannot allocate memory\n");
			return -ENOMEM;
		}

		do{
			res = tracking_collect(get.count, p_cbt_info, &get.count);
			if (SUCCESS != res){
				pr_err("Failed to execute tracking_collect. errno=%d\n", res);
				break;
			}
			if (0 != copy_to_user(get.p_cbt_info, p_cbt_info, get.count*sizeof(struct cbt_info_s))){
				pr_err("Unable to collect tracking devices: invalid user buffer for CBT info\n");
				res = -ENODATA;
				break;
			}

			if (0 != copy_to_user((void*)arg, (void*)&get, sizeof(struct ioctl_tracking_collect_s))){
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

int ioctl_tracking_block_size( unsigned long arg )
{
	unsigned int blk_sz = (1<<get_change_tracking_block_size_pow());

	if (0 != copy_to_user( (void*)arg, &blk_sz, sizeof( unsigned int ) )){
		pr_err( "Unable to get tracking block size: invalid user buffer for arguments\n" );
		return -ENODATA;
	}
	return SUCCESS;
}

int ioctl_tracking_read_cbt_map( unsigned long arg )
{
	struct ioctl_tracking_read_cbt_bitmap_s readbitmap;

	if (0 != copy_from_user( &readbitmap, (void*)arg, sizeof( struct ioctl_tracking_read_cbt_bitmap_s ) )){
		pr_err( "Unable to read CBT map: invalid user buffer\n" );
		return -ENODATA;
	}

	return tracking_read_cbt_bitmap(
		MKDEV( readbitmap.dev_id.major, readbitmap.dev_id.minor ),
		readbitmap.offset,
		readbitmap.length,
		(void*)readbitmap.buff
	);
}

int ioctl_tracking_mark_dirty_blocks(unsigned long arg)
{
	struct ioctl_tracking_mark_dirty_blocks_s param;
	struct block_range_s* p_dirty_blocks;
	size_t buffer_size;
	int result = SUCCESS;

	if (0 != copy_from_user(&param, (void*)arg, sizeof(struct ioctl_tracking_mark_dirty_blocks_s))){
		pr_err("Unable to mark dirty blocks: invalid user buffer\n");
		return -ENODATA;
	}

	buffer_size = param.count * sizeof(struct block_range_s);
	p_dirty_blocks = kzalloc(buffer_size, GFP_KERNEL);
	if (p_dirty_blocks == NULL){
		pr_err("Unable to mark dirty blocks: cannot allocate [%ld] bytes\n", buffer_size);
		return -ENOMEM;
	}

	do{
		if (0 != copy_from_user(p_dirty_blocks, (void*)param.p_dirty_blocks, buffer_size)){
			pr_err("Unable to mark dirty blocks: invalid user buffer\n");
			result = -ENODATA;
			break;
		}

		result = snapimage_mark_dirty_blocks(MKDEV(param.image_dev_id.major, param.image_dev_id.minor), p_dirty_blocks, param.count);
	} while (false);
	kfree(p_dirty_blocks);

	return result;
}

int ioctl_snapshot_create( unsigned long arg )
{
	size_t dev_id_buffer_size;
	int status;
	struct ioctl_snapshot_create_s param;
	struct ioctl_dev_id_s* pk_dev_id = NULL;

	if (0 != copy_from_user( &param, (void*)arg, sizeof( struct ioctl_snapshot_create_s ) )){
		pr_err( "Unable to create snapshot: invalid user buffer\n" );
		return -ENODATA;
	}

	dev_id_buffer_size = sizeof( struct ioctl_dev_id_s ) * param.count;
	pk_dev_id = kzalloc( dev_id_buffer_size, GFP_KERNEL );
	if (NULL == pk_dev_id){
		pr_err( "Unable to create snapshot: cannot allocate [%ld] bytes\n", dev_id_buffer_size );
		return -ENOMEM;
	}

	do{
		size_t dev_buffer_size;
		dev_t* p_dev = NULL;
		int inx = 0;

		if (0 != copy_from_user( pk_dev_id, (void*)param.p_dev_id, param.count*sizeof( struct ioctl_dev_id_s ) )){
			pr_err( "Unable to create snapshot: invalid user buffer for parameters\n" );
			status = -ENODATA;
			break;
		}

		dev_buffer_size = sizeof( dev_t ) * param.count;
		p_dev = kzalloc( dev_buffer_size, GFP_KERNEL );
		if (NULL == p_dev){
			pr_err( "Unable to create snapshot: cannot allocate [%ld] bytes\n", dev_buffer_size );
			status = -ENOMEM;
			break;
		}

		for (inx = 0; inx < param.count; ++inx)
			p_dev[inx] = MKDEV( pk_dev_id[inx].major, pk_dev_id[inx].minor );

		status = snapshot_Create(p_dev, param.count, get_change_tracking_block_size_pow(), &param.snapshot_id);

		kfree( p_dev );
		p_dev = NULL;

	} while (false);
	kfree( pk_dev_id );
	pk_dev_id = NULL;

	if (status == SUCCESS){
		if (0 != copy_to_user( (void*)arg, &param, sizeof( struct ioctl_snapshot_create_s ) )){
			pr_err( "Unable to create snapshot: invalid user buffer\n" );
			status = -ENODATA;
		}
	}

	return status;
}

int ioctl_snapshot_destroy( unsigned long arg )
{
	unsigned long long param;

	if (0 != copy_from_user( &param, (void*)arg, sizeof( unsigned long long ) )){
		pr_err( "Unable to destroy snapshot: invalid user buffer\n" );
		return -ENODATA;
	}

	return snapshot_Destroy( param );
}
//////////////////////////////////////////////////////////////////////////
int ioctl_snapstore_create( unsigned long arg )
{
	int res = SUCCESS;
	struct ioctl_snapstore_create_s param;
	struct ioctl_dev_id_s* pk_dev_id = NULL;
	size_t dev_id_buffer_size;

	if (0 != copy_from_user( &param, (void*)arg, sizeof( struct ioctl_snapstore_create_s ) )){
		pr_err( "Unable to create snapstore: invalid user buffer\n" );
		return -EINVAL;
	}

	dev_id_buffer_size = sizeof( struct ioctl_dev_id_s ) * param.count;
	pk_dev_id = kzalloc( dev_id_buffer_size, GFP_KERNEL );
	if (NULL == pk_dev_id){
		pr_err( "Unable to create snapstore: cannot allocate [%ld] bytes\n", dev_id_buffer_size );
		return -ENOMEM;
	}

	do{
		size_t inx = 0;
		dev_t* dev_id_set = NULL;
		uuid_t* id = (uuid_t*)param.id;
		dev_t snapstore_dev_id;
		size_t dev_id_set_length = (size_t)param.count;
		size_t dev_id_set_buffer_size;

		if ((0 == param.snapstore_dev_id.major) && (0 == param.snapstore_dev_id.minor))
			snapstore_dev_id = 0; //memory snapstore
		else if ((-1 == param.snapstore_dev_id.major) && (-1 == param.snapstore_dev_id.minor))
			snapstore_dev_id = 0xFFFFffff; //multidevice snapstore
		else
			snapstore_dev_id = MKDEV( param.snapstore_dev_id.major, param.snapstore_dev_id.minor ); //ordinal file snapstore

		if (0 != copy_from_user( pk_dev_id, (void*)param.p_dev_id, param.count*sizeof( struct ioctl_dev_id_s ) )){
			pr_err( "Unable to create snapstore: invalid user buffer for parameters\n" );
			res = -ENODATA;
			break;
		}

		dev_id_set_buffer_size = sizeof( dev_t ) * param.count;
		dev_id_set = kzalloc( dev_id_set_buffer_size, GFP_KERNEL );
		if (NULL == dev_id_set){
			pr_err( "Unable to create snapstore: cannot allocate [%ld] bytes\n", dev_id_set_buffer_size );
			res = -ENOMEM;
			break;
		}

		for (inx = 0; inx < dev_id_set_length; ++inx)
			dev_id_set[inx] = MKDEV( pk_dev_id[inx].major, pk_dev_id[inx].minor );

		res = snapstore_create( id, snapstore_dev_id, dev_id_set, dev_id_set_length );

		kfree( dev_id_set );
	} while (false);
	kfree( pk_dev_id );

	return res;
}

int ioctl_snapstore_file( unsigned long arg )
{
	int res = SUCCESS;
	struct ioctl_snapstore_file_add_s param;
	struct big_buffer *ranges = NULL;
	size_t ranges_buffer_size;

	if (0 != copy_from_user( &param, (void*)arg, sizeof( struct ioctl_snapstore_file_add_s ) )){
		pr_err( "Unable to add file to snapstore: invalid user buffer\n" );
		return -EINVAL;
	}

	ranges_buffer_size = sizeof( struct ioctl_range_s ) * param.range_count;

	ranges = big_buffer_alloc( ranges_buffer_size, GFP_KERNEL );
	if (NULL == ranges){
		pr_err( "Unable to add file to snapstore: cannot allocate [%ld] bytes\n", ranges_buffer_size );
		return -ENOMEM;
	}

	do{
		uuid_t* id = (uuid_t*)(param.id);
		size_t ranges_cnt = (size_t)param.range_count;

		if (ranges_buffer_size != big_buffer_copy_from_user( (void*)param.ranges, 0, ranges, ranges_buffer_size ) ){
			pr_err( "Unable to add file to snapstore: invalid user buffer for parameters\n" );
			res = -ENODATA;
			break;
		}

		res = snapstore_add_file( id, ranges, ranges_cnt );
	}while (false);
	big_buffer_free( ranges );

	return res;
}

int ioctl_snapstore_memory( unsigned long arg )
{
	int res = SUCCESS;
	struct ioctl_snapstore_memory_limit_s param;

	if (0 != copy_from_user( &param, (void*)arg, sizeof( struct ioctl_snapstore_memory_limit_s ) )){
		pr_err( "Unable to add memory block to snapstore: invalid user buffer\n" );
		return -EINVAL;
	}

	res = snapstore_add_memory( (uuid_t*)param.id, param.size );

	return res;
}
int ioctl_snapstore_cleanup( unsigned long arg )
{
	int res = SUCCESS;
	struct ioctl_snapstore_cleanup_s param;

	if (0 != copy_from_user( &param, (void*)arg, sizeof( struct ioctl_snapstore_cleanup_s ) )){
		pr_err( "Unable to perform snapstore cleanup: invalid user buffer\n" );
		return -EINVAL;
	}


	pr_err("id=%pUB\n", (uuid_t*)param.id);
	res = snapstore_cleanup((uuid_t*)param.id, &param.filled_bytes);

	if (res == SUCCESS){
		if (0 != copy_to_user( (void*)arg, &param, sizeof( struct ioctl_snapstore_cleanup_s ) )){
			pr_err( "Unable to perform snapstore cleanup: invalid user buffer\n" );
			res = -ENODATA;
		}
	}

	return res;
}

#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
int ioctl_snapstore_file_multidev( unsigned long arg )
	{
	int res = SUCCESS;
	struct ioctl_snapstore_file_add_multidev_s param;
	struct big_buffer *ranges = NULL;//struct ioctl_range_s* ranges = NULL;
	size_t ranges_buffer_size;

	if (0 != copy_from_user( &param, (void*)arg, sizeof( struct ioctl_snapstore_file_add_multidev_s ) )){
		pr_err( "Unable to add file to multidev snapstore: invalid user buffer\n" );
		return -EINVAL;
	}

	ranges_buffer_size = sizeof( struct ioctl_range_s ) * param.range_count;

	ranges = big_buffer_alloc( ranges_buffer_size, GFP_KERNEL );
	if (NULL == ranges){
		pr_err( "Unable to add file to multidev snapstore: cannot allocate [%ld] bytes\n", ranges_buffer_size );
		return -ENOMEM;
	}

	do{
		uuid_t* id = (uuid_t*)(param.id);
		dev_t snapstore_device = MKDEV( param.dev_id.major, param.dev_id.minor );
		size_t ranges_cnt = (size_t)param.range_count;

		if (ranges_buffer_size != big_buffer_copy_from_user( (void*)param.ranges, 0, ranges, ranges_buffer_size )){
			pr_err( "Unable to add file to snapstore: invalid user buffer for parameters\n" );
			res = -ENODATA;
			break;
		}

		res = snapstore_add_multidev( id, snapstore_device, ranges, ranges_cnt );
	} while (false);
	big_buffer_free( ranges );

	return res;
}

#endif
//////////////////////////////////////////////////////////////////////////

/**
  * Snapshot get errno for device
  */
int ioctl_snapshot_errno( unsigned long arg )
{
	int res;
	struct ioctl_snapshot_errno_s param;
	
	if (0 != copy_from_user( &param, (void*)arg, sizeof( struct ioctl_dev_id_s ) )) {
		pr_err( "Unable failed to get snapstore error code: invalid user buffer\n" );
		return -EINVAL;
	}

	res = snapstore_device_errno( MKDEV( param.dev_id.major, param.dev_id.minor ), &param.err_code );

	if (res != SUCCESS)
		return res;

	if (0 != copy_to_user( (void*)arg, &param, sizeof( struct ioctl_snapshot_errno_s ) )){
		pr_err( "Unable to get snapstore error code: invalid user buffer\n" );
		return -EINVAL;
	}

	return SUCCESS;
}

int ioctl_collect_snapimages( unsigned long arg )
{
	int status = SUCCESS;
	struct ioctl_collect_shapshot_images_s param;

	if (0 != copy_from_user( &param, (void*)arg, sizeof( struct ioctl_collect_shapshot_images_s ) )){
		pr_err( "Unable to collect snapshot images: invalid user buffer\n" );
		return -ENODATA;
	}

	status = snapimage_collect_images( param.count, param.p_image_info, &param.count );

	if (0 != copy_to_user( (void*)arg, &param, sizeof( struct ioctl_collect_shapshot_images_s ) )){
		pr_err( "Unable to collect snapshot images: invalid user buffer\n" );
		return -ENODATA;
	}

	return status;
}

typedef int (veeam_ioctl_t)(unsigned long arg);
typedef struct veeam_ioctl_table_s {
	unsigned int cmd;
	veeam_ioctl_t* fn;
} veeam_ioctl_table_t;

static veeam_ioctl_table_t veeam_ioctl_table[] =
{
	{ (IOCTL_COMPATIBILITY_FLAGS), ioctl_compatibility_flags },
	{ (IOCTL_GETVERSION), ioctl_get_version },

	{ (IOCTL_TRACKING_ADD), ioctl_tracking_add },
	{ (IOCTL_TRACKING_REMOVE), ioctl_tracking_remove },
	{ (IOCTL_TRACKING_COLLECT), ioctl_tracking_collect },
	{ (IOCTL_TRACKING_BLOCK_SIZE), ioctl_tracking_block_size },
	{ (IOCTL_TRACKING_READ_CBT_BITMAP), ioctl_tracking_read_cbt_map },
	{ (IOCTL_TRACKING_MARK_DIRTY_BLOCKS), ioctl_tracking_mark_dirty_blocks},

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

long ctrl_unlocked_ioctl( struct file *filp, unsigned int cmd, unsigned long arg )
{
	long status = -ENOTTY;
	size_t inx = 0;

	while (veeam_ioctl_table[inx].cmd != 0){
		if (veeam_ioctl_table[inx].cmd == cmd){
			status = veeam_ioctl_table[inx].fn( arg );
			break;
		}
		++inx;
	}

	return status;
}

