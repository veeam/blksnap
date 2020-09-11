#include "common.h"
#include "tracker.h"
#include "blk_util.h"
#define SECTION "tracker   "
#include "log_format.h"

LIST_HEAD(trackers);
DEFINE_RWLOCK(trackers_lock);

void tracker_done(void )
{
	tracker_remove_all();
}

int tracker_find_by_queue(tracker_queue_t* tq, tracker_t** ptracker)
{
	int result = -ENODATA;

	read_lock( &trackers_lock );
	if (!list_empty( &trackers )) {
		struct list_head* _head;

		list_for_each( _head, &trackers ) {
			tracker_t* _tracker = list_entry( _head, tracker_t, link );

			if (tq == _tracker->tracker_queue) {
				if (ptracker != NULL)
					*ptracker = _tracker;

				result = SUCCESS;
				break;
			}
		}
	}
	read_unlock( &trackers_lock );

	return result;
}

int tracker_find_by_dev_id( dev_t dev_id, tracker_t** ptracker )
{
	int result = -ENODATA;

	read_lock( &trackers_lock );
	if (!list_empty( &trackers )) {
		struct list_head* _head;

		list_for_each( _head, &trackers ) {
			tracker_t* _tracker = list_entry( _head, tracker_t, link );

			if (_tracker->original_dev_id == dev_id) {
				if (ptracker != NULL)
					*ptracker = _tracker;

				result =  SUCCESS;
				break;
			}
		}
	}
	read_unlock( &trackers_lock );

	return result;
}

int tracker_enum_cbt_info( int max_count, struct cbt_info_s* p_cbt_info, int* p_count )
{
	int result = SUCCESS;
	int count = 0;


	read_lock( &trackers_lock );
	if (!list_empty( &trackers )) {
		struct list_head* _head;

		list_for_each( _head, &trackers ) {
			tracker_t* tracker = list_entry( _head, tracker_t, link );

			if (count >= max_count){
				result = -ENOBUFS;
				break;	//don`t continue
			}

			if (p_cbt_info != NULL){
				p_cbt_info[count].dev_id.major = MAJOR(tracker->original_dev_id);
				p_cbt_info[count].dev_id.minor = MINOR(tracker->original_dev_id);

				if (tracker->cbt_map){
					p_cbt_info[count].cbt_map_size = tracker->cbt_map->map_size;
					p_cbt_info[count].snap_number = (unsigned char)tracker->cbt_map->snap_number_previous;
					uuid_copy((uuid_t*)(p_cbt_info[count].generationId), &tracker->cbt_map->generationId);
				}
				else{
					p_cbt_info[count].cbt_map_size = 0;
					p_cbt_info[count].snap_number = 0;
				}

				p_cbt_info[count].dev_capacity = sector_to_streamsize(blk_dev_get_capacity(tracker->target_dev));
			}

			++count;
		}
	}
	read_unlock( &trackers_lock );

	if (SUCCESS == result)
		if (count == 0)
			result = -ENODATA;

	*p_count = count;
	return result;
}

void tracker_cbt_start(tracker_t* tracker, unsigned long long snapshot_id, cbt_map_t* cbt_map)
{
	tracker_snapshot_id_set(tracker, snapshot_id);
	tracker->cbt_map = cbt_map_get_resource(cbt_map);
}

int tracker_create(unsigned long long snapshot_id, dev_t dev_id, unsigned int cbt_block_size_degree, cbt_map_t* cbt_map, tracker_t** ptracker)
{
	int result = SUCCESS;
	tracker_t* tracker = NULL;

	*ptracker = NULL;

	tracker = kzalloc(sizeof(tracker_t), GFP_KERNEL);
	if (NULL==tracker)
		return -ENOMEM;
	INIT_LIST_HEAD(&tracker->link);

	atomic_set( &tracker->is_captured, false);
	tracker->is_unfreezable = false;
	init_rwsem(&tracker->unfreezable_lock);

	tracker->original_dev_id = dev_id;

	result = blk_dev_open( tracker->original_dev_id, &tracker->target_dev );
	if (result != SUCCESS) {
		kfree(tracker);
		return result;
	}

	write_lock(&trackers_lock);
	list_add_tail(&tracker->link, &trackers);
	write_unlock(&trackers_lock);

	do{
		struct super_block* superblock = NULL;

		log_tr_format( "Create tracker for device [%d:%d]. Capacity 0x%llx sectors",
			MAJOR( tracker->original_dev_id ), MINOR( tracker->original_dev_id ),
			(unsigned long long)blk_dev_get_capacity(tracker->target_dev));


		if (cbt_map == NULL){
			cbt_map = cbt_map_create(cbt_block_size_degree-SECTOR_SHIFT, blk_dev_get_capacity(tracker->target_dev));
			if (cbt_map == NULL){
				result = -ENOMEM;
				break;
			}
			tracker_cbt_start(tracker, snapshot_id, cbt_map);
		}
		else
			tracker_cbt_start(tracker, snapshot_id, cbt_map);

		result = blk_freeze_bdev( tracker->original_dev_id, tracker->target_dev, &superblock );
		if (result != SUCCESS){
			tracker->is_unfreezable = true;
			break;
		}

		result = tracker_queue_ref(tracker->target_dev->bd_disk, tracker->target_dev->bd_partno, &tracker->tracker_queue);

		superblock = blk_thaw_bdev( tracker->original_dev_id, tracker->target_dev, superblock );

	}while(false);

	if (SUCCESS ==result){
		*ptracker = tracker;
	}else{
		int remove_status = SUCCESS;

		log_err_dev_t( "Failed to create tracker for device ", tracker->original_dev_id );

		remove_status = tracker_remove(tracker);
		if ((SUCCESS == remove_status) || (-ENODEV == remove_status))
			tracker = NULL;
		else
			log_err_d( "Failed to perfrom tracker cleanup. errno=", (0 - remove_status) );
		}

	return result;
}

int _tracker_remove( tracker_t* tracker )
{
	int result = SUCCESS;

	if (NULL != tracker->target_dev){

		struct super_block* superblock = NULL;

		if (tracker->is_unfreezable)
			down_write(&tracker->unfreezable_lock);
		else
			result = blk_freeze_bdev(tracker->original_dev_id, tracker->target_dev, &superblock);

		if (NULL != tracker->tracker_queue){
			tracker_queue_unref( tracker->tracker_queue );
			tracker->tracker_queue = NULL;
		}
		if (tracker->is_unfreezable)
			up_write(&tracker->unfreezable_lock);
		else
			superblock = blk_thaw_bdev(tracker->original_dev_id, tracker->target_dev, superblock);

		blk_dev_close( tracker->target_dev );

		tracker->target_dev = NULL;
	}else
		result=-ENODEV;

	if (NULL != tracker->cbt_map){
		cbt_map_put_resource(tracker->cbt_map);
		tracker->cbt_map = NULL;
	}

	return result;
}


int tracker_remove(tracker_t* tracker)
{
	int result = _tracker_remove( tracker );

	write_lock(&trackers_lock);
	list_del(&tracker->link);
	write_unlock(&trackers_lock);

	kfree(tracker);

	return result;
}

void tracker_remove_all(void )
{
	tracker_t* tracker;

	log_tr("Removing all devices from tracking");

	do {
		tracker = NULL;

		write_lock( &trackers_lock );
		if (!list_empty( &trackers )){
			tracker_t* tracker = list_entry( trackers.next, tracker_t, link );

			list_del( &tracker->link );
		}
		write_unlock(&trackers_lock);

		if (tracker) {
			int status = _tracker_remove( tracker );

			if (status != SUCCESS)
				log_err_format( "Failed to remove device [%d:%d] from tracking. errno=%d",
					MAJOR( tracker->original_dev_id ),
					MINOR( tracker->original_dev_id ),
					0 - status );

			kfree(tracker);
		}
	}while(tracker);
}

void tracker_cbt_bitmap_set( tracker_t* tracker, sector_t sector, sector_t sector_cnt )
{
	if (tracker->cbt_map == NULL)
		return;

	if (tracker->cbt_map->device_capacity != blk_dev_get_capacity(tracker->target_dev)){
		log_warn("Device resize detected");
		tracker->cbt_map->active = false;
		return;
	}

	if (SUCCESS != cbt_map_set(tracker->cbt_map, sector, sector_cnt)){ //cbt corrupt
		log_warn( "CBT fault detected" );
		tracker->cbt_map->active = false;
		return;
	}
}

bool tracker_cbt_bitmap_lock( tracker_t* tracker )
{
	bool result = false;
	if (tracker->cbt_map){
		cbt_map_read_lock( tracker->cbt_map );

		if (tracker->cbt_map->active){
			result = true;
		}
		else
			cbt_map_read_unlock( tracker->cbt_map );
	}
	return result;
}

void tracker_cbt_bitmap_unlock( tracker_t* tracker )
{
	if (tracker->cbt_map)
		cbt_map_read_unlock( tracker->cbt_map );
}

int _tracker_capture_snapshot( tracker_t* tracker )
{
	int result = SUCCESS;
	defer_io_t* defer_io = NULL;

	result = defer_io_create( tracker->original_dev_id, tracker->target_dev, &defer_io );
	if (result != SUCCESS){
		log_err( "Failed to create defer IO processor" );
	}else{
		tracker->defer_io = defer_io_get_resource( defer_io );

		atomic_set( &tracker->is_captured, true );

		if (tracker->cbt_map != NULL){
			cbt_map_write_lock( tracker->cbt_map );
			cbt_map_switch( tracker->cbt_map );
			cbt_map_write_unlock( tracker->cbt_map );

			log_tr_format( "Snapshot captured for device [%d:%d]. New snap number %ld",
				MAJOR( tracker->original_dev_id ), MINOR( tracker->original_dev_id ), tracker->cbt_map->snap_number_active );
		}
	}

	return result;

}

int tracker_capture_snapshot( snapshot_t* snapshot )
{
	int result = SUCCESS;
	int inx = 0;

	for (inx = 0; inx < snapshot->dev_id_set_size; ++inx){
		struct super_block* superblock = NULL;
		tracker_t* tracker = NULL;
		dev_t dev_id = snapshot->dev_id_set[inx];

		result = tracker_find_by_dev_id( dev_id, &tracker );
		if (result != SUCCESS){
			log_err_dev_t( "Unable to capture snapshot: cannot find device ", dev_id );
			break;
		}


		if (tracker->is_unfreezable)
			down_write(&tracker->unfreezable_lock);
		else
			blk_freeze_bdev(tracker->original_dev_id, tracker->target_dev, &superblock);
		{
			result = _tracker_capture_snapshot( tracker );
			if (result != SUCCESS)
				log_err_dev_t( "Failed to capture snapshot for device ", dev_id );
		}
		if (tracker->is_unfreezable)
			up_write(&tracker->unfreezable_lock);
		else
			superblock = blk_thaw_bdev(tracker->original_dev_id, tracker->target_dev, superblock);
	}
	if (result != SUCCESS)
		return result;

	for (inx = 0; inx < snapshot->dev_id_set_size; ++inx){
		tracker_t* p_tracker = NULL;
		dev_t dev_id = snapshot->dev_id_set[inx];

		result = tracker_find_by_dev_id( dev_id, &p_tracker );
		if (result != SUCCESS){
			log_err_dev_t("Unable to capture snapshot: cannot find device ", dev_id);
			continue;
		}

		if (snapstore_device_is_corrupted( p_tracker->defer_io->snapstore_device )){
			log_err_format( "Unable to freeze devices [%d:%d]: snapshot data is corrupted", MAJOR(dev_id), MINOR(dev_id) );
			result = -EDEADLK;
			break;
		}
	}

	if (result != SUCCESS){
		int status;
		log_err_d( "Failed to capture snapshot. errno=", result );

		status = tracker_release_snapshot( snapshot );
		if (status != SUCCESS)
			log_err_d( "Failed to perfrom snapshot cleanup. errno= ", status );
	}
	return result;
}

int _tracker_release_snapshot( tracker_t* tracker )
{
	int result = SUCCESS;
	struct super_block* superblock = NULL;
	defer_io_t* defer_io = tracker->defer_io;


	if (tracker->is_unfreezable)
		down_write(&tracker->unfreezable_lock);
	else
		result = blk_freeze_bdev(tracker->original_dev_id, tracker->target_dev, &superblock);
	{//locked region
		atomic_set(&tracker->is_captured, false);//clear freeze flag

		tracker->defer_io = NULL;
	}
	if (tracker->is_unfreezable)
		up_write(&tracker->unfreezable_lock);
	else
		superblock = blk_thaw_bdev(tracker->original_dev_id, tracker->target_dev, superblock);

	defer_io_stop(defer_io);
	defer_io_put_resource(defer_io);

	return result;
}


int tracker_release_snapshot( snapshot_t* snapshot )
{
	int result = SUCCESS;
	int inx = 0;
	log_tr_format( "Release snapshot [0x%llx]", snapshot->id );

	for (; inx < snapshot->dev_id_set_size; ++inx){
		int status;
		tracker_t* p_tracker = NULL;
		dev_t dev = snapshot->dev_id_set[inx];

		status = tracker_find_by_dev_id( dev, &p_tracker );
		if (status == SUCCESS){
			status = _tracker_release_snapshot( p_tracker );
			if (status != SUCCESS){
				log_err_dev_t( "Failed to release snapshot for device ", dev );
				result = status;
				break;
			}
		}
		else
			log_err_dev_t( "Unable to release snapshot: cannot find tracker for device ", dev );
	}

	return result;
}

void tracker_snapshot_id_set(tracker_t* tracker, unsigned long long snapshot_id)
{
	tracker->snapshot_id = snapshot_id;
}

unsigned long long tracker_snapshot_id_get(tracker_t* tracker)
{
	return tracker->snapshot_id;
}
