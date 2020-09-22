#include "common.h"
#include "snapstore.h"
#include "snapstore_device.h"
#include "snapstore_blk.h"
#include "big_buffer.h"

#define SECTION "snapstore "
#include "log_format.h"

LIST_HEAD(snapstores);
DECLARE_RWSEM(snapstores_lock);

bool _snapstore_check_halffill( snapstore_t* snapstore, sector_t* fill_status )
{
	blk_descr_pool_t* pool = NULL;

	if (snapstore->file)
		pool = &snapstore->file->pool;
#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
	else if (snapstore->multidev)
		pool = &snapstore->multidev->pool;
#endif
	else if (snapstore->mem)
		pool = &snapstore->mem->pool;

	if (pool)
		return blk_descr_pool_check_halffill( pool, snapstore->empty_limit, fill_status );
	
	return false;
}

void _snapstore_destroy( snapstore_t* snapstore )
{
	sector_t fill_status;

	log_tr_uuid( "Destroy snapstore with id=", (&snapstore->id) );

	_snapstore_check_halffill( snapstore, &fill_status );

	down_write(&snapstores_lock);
	list_del(&snapstore->link);
	up_write(&snapstores_lock);	

	if (snapstore->mem != NULL)
		snapstore_mem_destroy( snapstore->mem );
	if (snapstore->multidev != NULL)
		snapstore_multidev_destroy( snapstore->multidev );
	if (snapstore->file != NULL)
		snapstore_file_destroy( snapstore->file );

	if (snapstore->ctrl_pipe){

		ctrl_pipe_t* pipe = snapstore->ctrl_pipe;
		snapstore->ctrl_pipe = NULL;

		ctrl_pipe_request_terminate( pipe, fill_status );

		ctrl_pipe_put_resource( pipe );
	}

	kfree(snapstore);
}

static
void _snapstore_destroy_cb( struct kref *kref )
{
	snapstore_t* snapstore = container_of(kref, snapstore_t, refcount);
	
	_snapstore_destroy(snapstore);
}

snapstore_t* snapstore_get( snapstore_t* snapstore )
{
	if(snapstore)
		kref_get( &snapstore->refcount );

	return snapstore;
};

void snapstore_put( snapstore_t* snapstore )
{
	if (snapstore)
		kref_put( &snapstore->refcount, _snapstore_destroy_cb );
};

void snapstore_done( )
{
	bool is_empty;
	down_read(&snapstores_lock);
	is_empty = list_empty(&snapstores);
	up_read(&snapstores_lock);

	//BUG_ON(!is_empty)
	if (!is_empty)
		log_err( "Unable to perform snapstore cleanup: container is not empty" );
}

int snapstore_create( uuid_t* id, dev_t snapstore_dev_id, dev_t* dev_id_set, size_t dev_id_set_length )
{
	int res = SUCCESS;
	size_t dev_id_inx;
	snapstore_t* snapstore = NULL;

	if (dev_id_set_length == 0)
		return -EINVAL;

	snapstore = kzalloc(sizeof(snapstore_t), GFP_KERNEL);
	if (snapstore == NULL)
		return -ENOMEM;

	INIT_LIST_HEAD( &snapstore->link );
	uuid_copy( &snapstore->id, id );

	log_tr_uuid( "Create snapstore with id ", (&snapstore->id) );

	snapstore->mem = NULL;
	snapstore->multidev = NULL;
	snapstore->file = NULL;

	snapstore->ctrl_pipe = NULL;
	snapstore->empty_limit = (sector_t)(64 * (1024 * 1024 / SECTOR_SIZE)); //by default value
	snapstore->halffilled = false;
	snapstore->overflowed = false;

	if (snapstore_dev_id == 0)
		log_tr( "Memory snapstore create" );

#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
	else if (snapstore_dev_id == 0xFFFFffff){
		snapstore_multidev_t* multidev = NULL;
		res = snapstore_multidev_create( &multidev );
		if (res != SUCCESS){
			kfree(snapstore);

			log_err_uuid( "Failed to create multidevice snapstore ", id );
			return res;
		}
		snapstore->multidev = multidev;
	}
#endif
	else {
		snapstore_file_t* file = NULL;
		res = snapstore_file_create( snapstore_dev_id, &file );
		if (res != SUCCESS){
			kfree(snapstore);

			log_err_uuid( "Failed to create snapstore file for snapstore ", id );
			return res;
		}
		snapstore->file = file;
	}

	down_write(&snapstores_lock);
	list_add_tail(&snapstores, &snapstore->link);
	up_write(&snapstores_lock);

	kref_init( &snapstore->refcount );

	for (dev_id_inx = 0; dev_id_inx < dev_id_set_length; ++dev_id_inx){
		res = snapstore_device_create( dev_id_set[dev_id_inx], snapstore );
		if (res != SUCCESS)
			break;
	}

	if (res != SUCCESS){
		snapstore_device_cleanup( id );
	}
	snapstore_put( snapstore );
	return res;
}

#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
int snapstore_create_multidev(uuid_t* id, dev_t* dev_id_set, size_t dev_id_set_length)
{
	int res = SUCCESS;
	size_t dev_id_inx;
	snapstore_t* snapstore = NULL;
	snapstore_multidev_t* multidev = NULL;

	if (dev_id_set_length == 0)
		return -EINVAL;

	snapstore = kzalloc(sizeof(snapstore_t), GFP_KERNEL);
	if (snapstore == NULL)
		return -ENOMEM;

	INIT_LIST_HEAD(&snapstore->link);

	uuid_copy( &snapstore->id, id );

	log_tr_uuid( "Create snapstore with id ", (&snapstore->id) );

	snapstore->mem = NULL;
	snapstore->file = NULL;
	snapstore->multidev = NULL;

	snapstore->ctrl_pipe = NULL;
	snapstore->empty_limit = (sector_t)(64 * (1024 * 1024 / SECTOR_SIZE)); //by default value
	snapstore->halffilled = false;
	snapstore->overflowed = false;


	res = snapstore_multidev_create( &multidev );
	if (res != SUCCESS){
		kfree(snapstore);

		log_err_uuid( "Failed to create snapstore file for snapstore ", id );
		return res;
	}
	snapstore->multidev = multidev;

	down_write(&snapstores_lock);
	list_add_tail( &snapstore->link, &snapstores );
	up_write(&snapstores_lock);

	kref_init( &snapstore->refcount );

	for (dev_id_inx = 0; dev_id_inx < dev_id_set_length; ++dev_id_inx){
		res = snapstore_device_create( dev_id_set[dev_id_inx], snapstore );
		if (res != SUCCESS)
			break;
	}

	if (res != SUCCESS){
		snapstore_device_cleanup( id );
	}
	snapstore_put( snapstore );
	return res;
}
#endif


int snapstore_cleanup( uuid_t* id, u64* filled_bytes )
{
	int res;
	sector_t filled;
	res = snapstore_check_halffill( id, &filled );
	if (res == SUCCESS){
		*filled_bytes = (u64)from_sectors( filled );

		log_tr_format( "Snapstore fill size: %lld MiB", (*filled_bytes >> 20) );
	}else{
		*filled_bytes = -1;
		log_err( "Failed to obtain snapstore data filled size" );
	}

	return snapstore_device_cleanup( id );
}

snapstore_t* _snapstore_find( uuid_t* id )
{
	snapstore_t* result = NULL;

	down_read( &snapstores_lock );
	if (!list_empty( &snapstores )) {
		struct list_head* _head;

		list_for_each( _head, &snapstores ) {
			snapstore_t* snapstore = list_entry( _head, snapstore_t, link );

			if (uuid_equal( &snapstore->id, id )){
				result = snapstore;
				break;
			}
		}
	}
	up_read( &snapstores_lock );

	return result;
}

int snapstore_stretch_initiate( uuid_t* unique_id, ctrl_pipe_t* ctrl_pipe, sector_t empty_limit )
{
	snapstore_t* snapstore = _snapstore_find( unique_id );
	if (NULL == snapstore){
		log_err_uuid( "Unable to initiate stretch snapstore: cannot find snapstore by uuid=", unique_id );
		return -ENODATA;
	}

	snapstore->ctrl_pipe = ctrl_pipe_get_resource( ctrl_pipe );
	snapstore->empty_limit = empty_limit;

	return SUCCESS;
}

int snapstore_add_memory( uuid_t* id, unsigned long long sz )
{
	int res = SUCCESS;
	snapstore_t* snapstore = NULL;


	log_tr_format( "Adding %lld bytes to the snapstore", sz );

	snapstore = _snapstore_find( id );
	if (snapstore == NULL){
		log_err_uuid( "Unable to add memory block to the snapstore: cannot found snapstore by id ", id );
		return -ENODATA;
	}

	if (snapstore->file != NULL){
		log_err( "Unable to add memory block to the snapstore: snapstore file is already created" );
		return -EINVAL;
	}
#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
	if (snapstore->multidev != NULL){
		log_err( "Unable to add memory block to the snapstore: snapstore multidevice is already created" );
		return -EINVAL;
	}
#endif
	if (snapstore->mem != NULL){
		log_err( "Unable to add memory block to the snapstore: snapstore memory buffer is already created" );
		return -EINVAL;
	}

	{
		size_t available_blocks = (size_t)(sz >> (snapstore_block_shift() + SECTOR_SHIFT));
		size_t current_block = 0;

		snapstore->mem = snapstore_mem_create( available_blocks );
		for (current_block = 0; current_block < available_blocks; ++current_block)
		{
			void* buffer = snapstore_mem_get_block( snapstore->mem );
			if (NULL == buffer){
				log_err( "Unable to add memory block to snapstore: not enough memory" );
				res = -ENOMEM;
				break;
			}

			res = blk_descr_mem_pool_add( &snapstore->mem->pool, buffer );
			if (res != SUCCESS){
				log_err( "Unable to add memory block to snapstore: failed to initialize new block" );
				break;
			}
		}
		if (res != SUCCESS){
			snapstore_mem_destroy( snapstore->mem );
			snapstore->mem = NULL;
		}
	}
	return res;
}

int rangelist_add( struct list_head *rglist, struct blk_range* rg )
{
	blk_range_link_t* range_link = kzalloc( sizeof( blk_range_link_t ), GFP_KERNEL );
	if (range_link == NULL)
		return -ENOMEM;

	INIT_LIST_HEAD( &range_link->link );

	range_link->rg.ofs = rg->ofs;
	range_link->rg.cnt = rg->cnt;

	list_add_tail( &range_link->link, rglist );

	return SUCCESS;
}

int snapstore_add_file( uuid_t* id, struct big_buffer *ranges, size_t ranges_cnt )
{
	int res = SUCCESS;
	snapstore_t* snapstore = NULL;
	snapstore_device_t* snapstore_device = NULL;
	sector_t current_blk_size = 0;
	LIST_HEAD( blk_rangelist );
	size_t inx;

	log_tr_format( "Snapstore add %ld ranges", ranges_cnt );

	if ((ranges_cnt == 0) || (ranges == NULL))
		return -EINVAL;

	snapstore = _snapstore_find( id );
	if (snapstore == NULL){
		log_err_uuid( "Unable to add file to snapstore: cannot find snapstore by id ", id );
		return -ENODATA;
	}

	if (snapstore->file == NULL){
		log_err( "Unable to add file to snapstore: snapstore file was not initialized");
		return -EFAULT;
	}

	snapstore_device = snapstore_device_find_by_dev_id( snapstore->file->blk_dev_id ); //for zeroed

	for (inx = 0; inx < ranges_cnt; ++inx) {
		size_t blocks_count = 0;
		sector_t range_offset = 0;

		struct blk_range range;
		struct ioctl_range_s* ioctl_range = (struct ioctl_range_s*)big_buffer_get_element( ranges, inx, sizeof( struct ioctl_range_s ) );

		if (NULL == ioctl_range) {
			log_err("Invalid count of ranges");
			res = -ENODATA;
			break;
		}

		range.ofs = (sector_t)to_sectors( ioctl_range->left );
		range.cnt = (blkcnt_t)to_sectors( ioctl_range->right ) - range.ofs;

		while (range_offset < range.cnt){
			struct blk_range rg;

			rg.ofs = range.ofs + range_offset;
			rg.cnt = min_t( sector_t, (range.cnt - range_offset), (snapstore_block_size() - current_blk_size) );

			range_offset += rg.cnt;

			res = rangelist_add( &blk_rangelist, &rg );
			if (res != SUCCESS){
				log_err( "Unable to add file to snapstore: cannot add range to rangelist" );
				break;
			}

			//zero sectors logic
			if (snapstore_device != NULL) {
				res = rangevector_add(&snapstore_device->zero_sectors, &rg);
				if (res != SUCCESS){
					log_err( "Unable to add file to snapstore: cannot add range to zero_sectors tree" );
					break;
				}
			}

			current_blk_size += rg.cnt;

			if (current_blk_size == snapstore_block_size()){//allocate  block
				res = blk_descr_file_pool_add( &snapstore->file->pool, &blk_rangelist );
				if (res != SUCCESS){
					log_err( "Unable to add file to snapstore: cannot initialize new block" );
					break;
				}

				snapstore->halffilled = false;

				current_blk_size = 0;
				INIT_LIST_HEAD( &blk_rangelist );//renew list
				++blocks_count;
			}
		}
		if (res != SUCCESS)
			break;
	}

	if ((res == SUCCESS) && (current_blk_size != 0))
		log_warn( "Snapstore portion was not ordered by Copy-on-Write block size" );

	return res;
}

#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
static
int rangelist_ex_add( struct list_head *list, struct blk_range* rg, struct block_device* blk_dev )
{
	blk_range_link_ex_t* range_link = kzalloc( sizeof( blk_range_link_ex_t ), GFP_KERNEL );
	if (range_link == NULL)
		return -ENOMEM;

	INIT_LIST_HEAD( &range_link->link );

	range_link->rg.ofs = rg->ofs;
	range_link->rg.cnt = rg->cnt;
	range_link->blk_dev = blk_dev;

	list_add_tail( &range_link->link, list );

	return SUCCESS;
}

int snapstore_add_multidev(uuid_t* id, dev_t dev_id, struct big_buffer *ranges, size_t ranges_cnt)
{
	int res = SUCCESS;
	snapstore_t* snapstore = NULL;
	sector_t current_blk_size = 0;
	size_t inx;
	LIST_HEAD(blk_rangelist);

	log_tr_format( "Snapstore add %ld ranges for device [%d:%d]", ranges_cnt, MAJOR(dev_id), MINOR(dev_id) );

	if ((ranges_cnt == 0) || (ranges == NULL))
		return -EINVAL;

	snapstore = _snapstore_find( id );
	if (snapstore == NULL){
		log_err_uuid( "Unable to add file to snapstore: cannot find snapstore by id ", id );
		return -ENODATA;
	}

	if (snapstore->multidev == NULL) {
		log_err( "Unable to add file to multidevice snapstore: multidevice snapstore was not initialized" );
		return -EFAULT;
	}

	for (inx = 0; inx < ranges_cnt; ++inx) {
		size_t blocks_count = 0;
		sector_t range_offset = 0;
		struct blk_range range;

		struct ioctl_range_s* data = (struct ioctl_range_s*)big_buffer_get_element( ranges, inx, sizeof( struct ioctl_range_s ) );

		if (NULL == data) {
			log_err("Invalid count of ranges");
			res = -ENODATA;
			break;
		}

		range.ofs = (sector_t)to_sectors( data->left );
		range.cnt = (blkcnt_t)to_sectors( data->right ) - range.ofs;

		//log_tr_format( "range=%lld:%lld", range.ofs, range.cnt );

		while (range_offset < range.cnt){
			struct blk_range rg;
			struct block_device* blk_dev = NULL;
			rg.ofs = range.ofs + range_offset;
			rg.cnt = min_t( sector_t, (range.cnt - range_offset), (snapstore_block_size() - current_blk_size) );

			range_offset += rg.cnt;

			//log_tr_range( "add rg=", rg );
			blk_dev = snapstore_multidev_get_device( snapstore->multidev, dev_id );
			if (NULL == blk_dev){
				log_err_format( "Cannot find or open device [%d:%d] for multidevice snapstore", MAJOR( dev_id ), MINOR( dev_id ) );
				res = -ENODEV;
				break;
			}

			res = rangelist_ex_add( &blk_rangelist, &rg, blk_dev );
			if (res != SUCCESS){
				log_err( "Unable to add file to snapstore: failed to add range to rangelist" );
				break;
			}

			/*
			 * zero sectors logic is not implemented to multidevice snapstore
			 */

			current_blk_size += rg.cnt;

			if (current_blk_size == snapstore_block_size()){//allocate  block
				res = blk_descr_multidev_pool_add( &snapstore->multidev->pool, &blk_rangelist );
				if (res != SUCCESS){
					log_err( "Unable to add file to snapstore: failed to initialize new block" );
					break;
				}

				snapstore->halffilled = false;

				current_blk_size = 0;
				INIT_LIST_HEAD(&blk_rangelist);
				++blocks_count;
			}
		}
		if (res != SUCCESS)
			break;
	}//for

	if ((res == SUCCESS) && (current_blk_size != 0))
		log_warn( "Snapstore portion was not ordered by Copy-on-Write block size" );

	return res;
}
#endif

void snapstore_order_border( struct blk_range* in, struct blk_range* out )
{
	struct blk_range unorder;

	unorder.ofs = in->ofs & snapstore_block_mask();
	out->ofs = in->ofs & ~snapstore_block_mask();
	out->cnt = in->cnt + unorder.ofs;

	unorder.cnt = out->cnt & snapstore_block_mask();
	if (unorder.cnt != 0)
		out->cnt += (snapstore_block_size() - unorder.cnt);
}

union blk_descr_unify snapstore_get_empty_block( snapstore_t* snapstore )
{
	union blk_descr_unify result = {NULL};

	if (snapstore->overflowed)
		return result;

	if (snapstore->file != NULL)
		result = blk_descr_file_pool_take( &snapstore->file->pool );
	else if (snapstore->multidev != NULL)
		result = blk_descr_multidev_pool_take( &snapstore->multidev->pool );
	else if (snapstore->mem != NULL)
		result = blk_descr_mem_pool_take( &snapstore->mem->pool );

	if (NULL == result.ptr){
		if (snapstore->ctrl_pipe){
			sector_t fill_status;
			_snapstore_check_halffill( snapstore, &fill_status );
			ctrl_pipe_request_overflow( snapstore->ctrl_pipe, -EINVAL, (u64)from_sectors( fill_status ) );
		}
		snapstore->overflowed = true;
	}

	return result;
}

int snapstore_check_halffill( uuid_t* unique_id, sector_t* fill_status )
{
	snapstore_t* snapstore = _snapstore_find( unique_id );
	if (NULL == snapstore){
		log_err_uuid( "Cannot find snapstore by uuid ", unique_id );
		return -ENODATA;
	}

	_snapstore_check_halffill( snapstore, fill_status );

	return SUCCESS;
}


int snapstore_request_store( snapstore_t* snapstore, blk_deferred_request_t* dio_copy_req )
{
	int res = SUCCESS;

	if (snapstore->ctrl_pipe){
		if (!snapstore->halffilled){
			sector_t fill_status = 0;

			if (_snapstore_check_halffill( snapstore, &fill_status )){
				snapstore->halffilled = true;
				ctrl_pipe_request_halffill( snapstore->ctrl_pipe, (u64)from_sectors( fill_status ) );
			}
		}
	}

	if (snapstore->file)
		res = blk_deferred_request_store_file( snapstore->file->blk_dev, dio_copy_req );
#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
	else if (snapstore->multidev)
		res = blk_deferred_request_store_multidev( dio_copy_req );
#endif
	else if (snapstore->mem)
		res = blk_deffered_request_store_mem( dio_copy_req );
	else
		res = -EINVAL;

	return res;
}

int snapstore_redirect_read( blk_redirect_bio_t* rq_redir, snapstore_t* snapstore, union blk_descr_unify blk_descr, sector_t target_pos, sector_t rq_ofs, sector_t rq_count )
{
	int res = SUCCESS;
	sector_t current_ofs = 0;
	sector_t block_ofs = target_pos & snapstore_block_mask();


	if (snapstore->file) {
		if (!list_empty( &blk_descr.file->rangelist )) {
			struct list_head* _list_head;

			list_for_each( _list_head, &blk_descr.file->rangelist ) {
				blk_range_link_t* range_link = list_entry( _list_head, blk_range_link_t, link );


				if (current_ofs >= rq_count)
					break;

				if (range_link->rg.cnt > block_ofs) {//read first portion from block

					sector_t pos = range_link->rg.ofs + block_ofs;
					sector_t len = min_t( sector_t, (range_link->rg.cnt - block_ofs), (rq_count - current_ofs) );

					res = blk_dev_redirect_part( rq_redir, READ, snapstore->file->blk_dev, pos, rq_ofs + current_ofs, len );
					if (res != SUCCESS){
						log_err_sect( "Failed to read from snapstore file. Sector #", pos );
						break;
					}

					current_ofs += len;
					block_ofs = 0;
				}
				else
					block_ofs -= range_link->rg.cnt;
			}
		}
	}
#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
	else if (snapstore->multidev) {
		if (!list_empty( &blk_descr.multidev->rangelist )) {
			struct list_head* _list_head;

			list_for_each( _list_head, &blk_descr.multidev->rangelist ) {
				blk_range_link_ex_t* range_link = list_entry( _list_head, blk_range_link_ex_t, link );

				if (current_ofs >= rq_count)
					break;

				if (range_link->rg.cnt > block_ofs) {//read first portion from block
					sector_t pos = range_link->rg.ofs + block_ofs;
					sector_t len = min_t( sector_t, (range_link->rg.cnt - block_ofs), (rq_count - current_ofs) );

					res = blk_dev_redirect_part( rq_redir, READ, range_link->blk_dev, pos, rq_ofs + current_ofs, len );

					if (res != SUCCESS) {
						log_err_sect( "Failed to read from snapstore file. Sector #", pos );
						break;
					}

					current_ofs += len;
					block_ofs = 0;
				} else
					block_ofs -= range_link->rg.cnt;
			}
		}

	}
#endif
	else if (snapstore->mem){
		res = blk_dev_redirect_memcpy_part( rq_redir, READ, blk_descr.mem->buff + (size_t)from_sectors( block_ofs ), rq_ofs, rq_count );
		if (res != SUCCESS){
			log_err( "Failed to read from snapstore memory" );
		}else
			current_ofs += rq_count;
	}
	else
		res = -EINVAL;

	if (res != SUCCESS){
		log_err_format( "Failed to read from snapstore. Offset %lld sector", target_pos );
	}

	return res;
}

int snapstore_redirect_write( blk_redirect_bio_t* rq_redir, snapstore_t* snapstore, union blk_descr_unify blk_descr, sector_t target_pos, sector_t rq_ofs, sector_t rq_count )
{
	int res = SUCCESS;
	sector_t current_ofs = 0;
	sector_t block_ofs = target_pos & snapstore_block_mask();

	BUG_ON( NULL == rq_redir );
	BUG_ON( NULL == snapstore );

	if (snapstore->file){
		if (!list_empty( &blk_descr.file->rangelist )){
			struct list_head* _list_head;

			list_for_each( _list_head, &blk_descr.file->rangelist ) {
				blk_range_link_t* range_link = list_entry( _list_head, blk_range_link_t, link );

				if (current_ofs >= rq_count)
					break;

				if (range_link->rg.cnt > block_ofs) {//read first portion from block
					sector_t pos = range_link->rg.ofs + block_ofs;
					sector_t len = min_t( sector_t, (range_link->rg.cnt - block_ofs), (rq_count - current_ofs) );

					res = blk_dev_redirect_part( rq_redir, WRITE, snapstore->file->blk_dev, pos, rq_ofs + current_ofs, len );

					if (res != SUCCESS){
						log_err_sect( "Failed to write to snapstore file. Sector #", pos );
						break;
					}

					current_ofs += len;
					block_ofs = 0;
				}
				else
					block_ofs -= range_link->rg.cnt;
			}
		}
	}
#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
	else if (snapstore->multidev) {
		if (!list_empty( &blk_descr.multidev->rangelist )) {
			struct list_head* _list_head;

			list_for_each( _list_head, &blk_descr.multidev->rangelist ) {
				blk_range_link_ex_t* range_link = list_entry( _list_head, blk_range_link_ex_t, link );

				if (current_ofs >= rq_count)
					break;

				if (range_link->rg.cnt > block_ofs) {//read first portion from block
					sector_t pos = range_link->rg.ofs + block_ofs;
					sector_t len = min_t( sector_t, (range_link->rg.cnt - block_ofs), (rq_count - current_ofs) );

					res = blk_dev_redirect_part( rq_redir, WRITE, range_link->blk_dev, pos, rq_ofs + current_ofs, len );

					if (res != SUCCESS){
						log_err_sect( "Failed to write to snapstore file. Sector #", pos );
						break;
					}

					current_ofs += len;
					block_ofs = 0;
				} else
					block_ofs -= range_link->rg.cnt;
			}
		}
	}
#endif
	else if (snapstore->mem){
		res = blk_dev_redirect_memcpy_part( rq_redir, WRITE,
			blk_descr.mem->buff + (size_t)from_sectors( block_ofs ), rq_ofs, rq_count );
		if (res != SUCCESS){
			log_err( "Failed to write to snapstore memory" );
		}
		else
			current_ofs += rq_count;
	}
	else{
		log_err( "Unable to write to snapstore: invalid type of snapstore device" );
		res = -EINVAL;
	}

	if (res != SUCCESS){
		log_err_format( "Failed to write to snapstore. Offset %lld sector", target_pos );
	}

	return res;
}
