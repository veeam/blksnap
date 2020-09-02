#include "stdafx.h"
#include "container.h"

#define SECTION "container "

static atomic_t g_container_alloc_cnt = ATOMIC_INIT( 0 );

int container_alloc_counter(void )
{
    return atomic_read( &g_container_alloc_cnt );
}

int container_init( container_t* pContainer, int content_size )
{
    INIT_LIST_HEAD( &pContainer->headList );
    init_rwsem( &pContainer->lock );

    pContainer->content_size = content_size;

    return SUCCESS;
}

int container_done( container_t* pContainer )
{
    int cnt;

    if (pContainer->content_size != 0){
        cnt = atomic_read( &pContainer->cnt );
        if ((cnt != 0) || !list_empty( &pContainer->headList )){
            log_err_d( "CRITICAL ERROR: Container is not empty. cnt=", cnt );
            return -EBUSY;
        }

        pContainer->content_size = 0;
    }
    return SUCCESS;
}

void container_print_state( void )
{
    log_tr( "" );
    log_tr( "Container state:" );
    log_tr_d( "Now allocated contents ", container_alloc_counter( ) );
}

content_t* container_new( container_t* pContainer )
{
    content_t* pCnt = content_new(pContainer);
    if (NULL != pCnt)
        container_push_back( pContainer, pCnt );
    return pCnt;
}

void _container_del( container_t* pContainer, content_t* pCnt )
{
    list_del( &pCnt->link );
    atomic_dec( &pContainer->cnt );
}

void _container_free( container_t* pContainer, content_t* pCnt )
{
    _container_del( pContainer, pCnt );
    content_free( pCnt );
}

void container_free( content_t* pCnt )
{
    container_t* pContainer = pCnt->container;
    down_write( &pContainer->lock );
    do{
        _container_free(pContainer, pCnt);
    }while(false);
    up_write( &pContainer->lock );
}
void container_get( content_t* pCnt )
{
    container_t* pContainer = pCnt->container;
    down_write( &pContainer->lock );
    do{
        list_del( &pCnt->link );
        atomic_dec( &pContainer->cnt );
    } while (false);
    up_write( &pContainer->lock );
}

int container_enum( container_t* pContainer, container_enum_cb_t callback, void* parameter )
{
    int result = SUCCESS;

    down_read( &pContainer->lock );
    do{
        if ( list_empty(&pContainer->headList) ){
            result = -ENODATA;
            break;
        }

        {
            content_t* pCnt = NULL;
            struct list_head* ptr;
            list_for_each( ptr, &pContainer->headList ){
                pCnt = list_entry(ptr, content_t, link);

                if ( SUCCESS==callback(pCnt, parameter) )
                    break;
            }
        }
    }while(false);
    up_read( &pContainer->lock );

    return result;
}

int container_enum_and_free( container_t* pContainer, container_enum_cb_t callback, void* parameter )
{
    int result = SUCCESS;

    down_write( &pContainer->lock );
    do{
        if ( list_empty(&pContainer->headList) ){
            break;
        }

        {
            content_t* pCnt = NULL;
            struct list_head* ptr;

            while( !list_empty( &pContainer->headList ) ){
                int ret;
                ptr = pContainer->headList.next;

                pCnt = list_entry(ptr, content_t, link);

                ret = callback(pCnt, parameter);

                if (ret < SUCCESS)
                    break;

                _container_free(pContainer, pCnt);

                if (SUCCESS==ret)
                    break;
            }
        }
    }while(false);
    up_write( &pContainer->lock );

    return result;
}


int container_length( container_t* pContainer )
{
    int length = 0;
    down_read( &pContainer->lock );
    do{
        length = atomic_read( &pContainer->cnt );
    }while(false);
    up_read( &pContainer->lock );

    return length;
}

bool container_empty( container_t* pContainer )
{
    return (0 == atomic_read( &pContainer->cnt ));
}

size_t container_push_back( container_t* pContainer, content_t* pCnt )
{
    size_t index = 0;

    down_write( &pContainer->lock );
    do{
        if (NULL != pCnt){
            INIT_LIST_HEAD( &pCnt->link );

            index = atomic_inc_return( &pContainer->cnt );
            list_add_tail( &pCnt->link, &pContainer->headList );
        }
    } while (false);
    up_write( &pContainer->lock );

    return index;
}

void container_push_top( container_t* pContainer, content_t* pCnt )
{
    down_write( &pContainer->lock );
    do{
        if (NULL != pCnt){
            INIT_LIST_HEAD( &pCnt->link );
            atomic_inc( &pContainer->cnt );
            list_add( &pCnt->link, &pContainer->headList );
        }
    } while (false);
    up_write( &pContainer->lock );

}

content_t* container_get_first( container_t* pContainer )
{
    content_t* pCnt = NULL;

    down_read( &pContainer->lock );
    if (0 != atomic_read( &pContainer->cnt )){
        pCnt = list_entry( pContainer->headList.next, content_t, link );

        list_del( &pCnt->link );
        atomic_dec( &pContainer->cnt );
    }
    up_read( &pContainer->lock );

    return pCnt;
}


content_t* content_new_opt( container_t* pContainer, gfp_t gfp_opt )
{
    content_t* pCnt = dbg_kmalloc( pContainer->content_size, gfp_opt );

    if (pCnt){
        atomic_inc( &g_container_alloc_cnt );

        memset( pCnt, 0, pContainer->content_size );

        pCnt->container = pContainer;
    }
    return pCnt;
}

content_t* content_new( container_t* pContainer )
{
    return content_new_opt(pContainer, GFP_KERNEL);
}

void content_free( content_t* pCnt )
{
    if (pCnt){
        container_t* pContainer = pCnt->container;

        memset( pCnt, 0xFF, pContainer->content_size );

        atomic_dec( &g_container_alloc_cnt );

        dbg_kfree( pCnt );
    }
}

