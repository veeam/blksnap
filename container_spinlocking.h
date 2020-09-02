#pragma once

typedef struct container_sl_s
{
    struct list_head headList;

    rwlock_t lock;

    int content_size;
    atomic_t cnt;
}container_sl_t;

typedef struct content_sl_s
{
    struct list_head link;
    container_sl_t* container;
}content_sl_t;

int container_sl_alloc_counter( void );

void container_sl_init( container_sl_t* pContainer, int content_size );
int container_sl_done( container_sl_t* pContainer );

void container_sl_print_state( void );

content_sl_t* container_sl_new( container_sl_t* pContainer );
void container_sl_free( content_sl_t* pCnt );

//typedef int( *container_sl_enum_cb_t )(content_sl_t* pCnt, void* parameter);

int container_sl_length( container_sl_t* pContainer );
bool container_sl_empty( container_sl_t* pContainer );

size_t container_sl_push_back( container_sl_t* pContainer, content_sl_t* pCnt );
content_sl_t* container_sl_get_first( container_sl_t* pContainer );

content_sl_t* content_sl_new_opt( container_sl_t* pContainer, gfp_t gfp_opt );
content_sl_t* content_sl_new( container_sl_t* pContainer );
void content_sl_free( content_sl_t* pCnt );

content_sl_t* container_sl_at( container_sl_t* pContainer, size_t inx ); // !!! CAUTION, very slow.


#define CONTAINER_SL_FOREACH_BEGIN(Container,content) \
read_lock( &Container.lock ); \
if (!list_empty( &Container.headList )){ \
    struct list_head* _container_list_head; \
    list_for_each( _container_list_head, &Container.headList ){ \
        content = list_entry( _container_list_head, content_sl_t, link );

#define CONTAINER_SL_FOREACH_END(Container) \
    } \
} \
read_unlock( &Container.lock );
