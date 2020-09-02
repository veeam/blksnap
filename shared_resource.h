#pragma once

typedef void (shared_resource_free_cb)(void* this_resource);

typedef struct shared_resource_s
{
    atomic_t own_cnt;
    void* this_resource;
    shared_resource_free_cb* free_cb;
}shared_resource_t;


static inline void shared_resource_init( shared_resource_t* resource_header, void* this_resource, shared_resource_free_cb* free_cb )
{
    atomic_set( &resource_header->own_cnt, 0 );
    resource_header->this_resource = this_resource;
    resource_header->free_cb = free_cb;
}

static inline void* shared_resource_get( shared_resource_t* resource_header )
{
    atomic_inc( &resource_header->own_cnt );
    return resource_header->this_resource;
}

static inline void shared_resource_put( shared_resource_t* resource_header )
{
    if (atomic_dec_and_test( &resource_header->own_cnt ))
        resource_header->free_cb( resource_header->this_resource );
}
