#pragma once

#include "page_array.h"
#include "shared_resource.h"
#include "uuid_util.h"

typedef struct cbt_map_s
{
    shared_resource_t sharing_header;

    spinlock_t locker;

    size_t   sect_in_block_degree;
    sector_t device_capacity;
    size_t   map_size;

    page_array_t*  read_map;
    page_array_t*  write_map;

    volatile unsigned long snap_number_active;
    volatile unsigned long snap_number_previous;
    veeam_uuid_t generationId;

    volatile bool active;

    struct rw_semaphore rw_lock;

    sector_t state_changed_sectors;
    sector_t state_dirty_sectors;
    
}cbt_map_t;

cbt_map_t* cbt_map_create(unsigned int cbt_sect_in_block_degree, sector_t device_capacity);
void cbt_map_destroy( cbt_map_t* cbt_map );

void cbt_map_switch( cbt_map_t* cbt_map );
int cbt_map_set( cbt_map_t* cbt_map, sector_t sector_start, sector_t sector_cnt );
int cbt_map_set_both( cbt_map_t* cbt_map, sector_t sector_start, sector_t sector_cnt );

size_t cbt_map_read_to_user( cbt_map_t* cbt_map, void __user * user_buffer, size_t offset, size_t size );


static inline cbt_map_t* cbt_map_get_resource( cbt_map_t* cbt_map )
{
    if (cbt_map == NULL)
        return NULL;

    return (cbt_map_t*)shared_resource_get( &cbt_map->sharing_header );
}

static inline void cbt_map_put_resource( cbt_map_t* cbt_map )
{
    if (cbt_map != NULL)
        shared_resource_put( &cbt_map->sharing_header );
}

static inline void cbt_map_read_lock( cbt_map_t* cbt_map )
{
    down_read( &cbt_map->rw_lock );
};
static inline void cbt_map_read_unlock( cbt_map_t* cbt_map )
{
    up_read( &cbt_map->rw_lock );
};
static inline void cbt_map_write_lock( cbt_map_t* cbt_map )
{
    down_write( &cbt_map->rw_lock );
};
static inline void cbt_map_write_unlock( cbt_map_t* cbt_map )
{
    up_write( &cbt_map->rw_lock );
};

void cbt_print_state(cbt_map_t* cbt_map);