#pragma once
#include "container_spinlocking.h"
#include "tracker_queue.h"
#include "cbt_map.h"
#include "defer_io.h"
#include "blk-snap-ctl.h"
#include "snapshot.h"

typedef struct tracker_s
{
	struct list_head link;

	dev_t original_dev_id;

	struct block_device* target_dev;
	tracker_queue_t* tracker_queue;

	cbt_map_t* cbt_map;

	atomic_t is_captured;

	bool is_unfreezable; // when device have not filesystem and can not be freeze
	struct rw_semaphore unfreezable_lock; //locking io processing for unfreezable devices

	defer_io_t* defer_io;

	volatile unsigned long long snapshot_id;		  // current snapshot for this device
}tracker_t;

void tracker_done( void );

int tracker_find_by_queue(tracker_queue_t* queue, tracker_t** ptracker);

int tracker_find_by_dev_id(dev_t dev_id, tracker_t** ptracker);
//int tracker_find_by_sb(struct super_block* sb, tracker_t** ptracker);

int tracker_enum_cbt_info(int max_count, struct cbt_info_s* p_cbt_info, int* p_count);

int tracker_capture_snapshot( snapshot_t* p_snapshot );
int tracker_release_snapshot( snapshot_t* p_snapshot );

void tracker_cbt_start(tracker_t* tracker, unsigned long long snapshot_id, cbt_map_t* cbt_map);

int tracker_create(unsigned long long snapshot_id, dev_t dev_id, unsigned int cbt_block_size_degree, cbt_map_t* cbt_map, tracker_t** ptracker);
int tracker_remove( tracker_t* tracker );
void tracker_remove_all( void );

void tracker_cbt_bitmap_set( tracker_t* tracker, sector_t sector, sector_t sector_cnt );

bool tracker_cbt_bitmap_lock( tracker_t* tracker );
void tracker_cbt_bitmap_unlock( tracker_t* tracker );

void tracker_snapshot_id_set(tracker_t* tracker, unsigned long long snapshot_id);
unsigned long long tracker_snapshot_id_get(tracker_t* tracker);

