#pragma once

typedef struct _container
{
    struct list_head headList;

    struct rw_semaphore lock;
    int content_size;
    atomic_t cnt;
}container_t;

typedef struct content_s
{
    struct list_head link;
    container_t* container;
}content_t;

int container_alloc_counter( void );

int container_init( container_t* pContainer, int content_size );
int container_done( container_t* pContainer );

void container_print_state( void );

content_t* container_new( container_t* pContainer );
void container_free( content_t* pCnt );
void container_get( content_t* pCnt ); //Content from container remove, but not free. For complete free use content_free.

typedef int( *container_enum_cb_t )(content_t* pCnt, void* parameter);
int container_enum( container_t* pContainer, container_enum_cb_t callback, void* parameter );
int container_enum_and_free( container_t* pContainer, container_enum_cb_t callback, void* parameter );

int container_length( container_t* pContainer );
bool container_empty( container_t* pContainer );

size_t container_push_back( container_t* pContainer, content_t* pCnt );
void container_push_top( container_t* pContainer, content_t* pCnt );
content_t* container_get_first( container_t* pContainer );

content_t* content_new_opt( container_t* pContainer, gfp_t gfp_opt );
content_t* content_new( container_t* pContainer );
void content_free( content_t* pCnt );

void _container_del( container_t* pContainer, content_t* pCnt ); //without locking

#define CONTAINER_FOREACH_BEGIN(Container,content) \
down_read( &Container.lock ); \
if (!list_empty( &Container.headList )){ \
    struct list_head* _container_list_head; \
    list_for_each( _container_list_head, &Container.headList ){ \
        content = list_entry( _container_list_head, content_t, link );


#define CONTAINER_FOREACH_END(Container) \
    } \
} \
up_read( &Container.lock );

