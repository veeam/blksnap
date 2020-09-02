#include "stdafx.h"
#include "snapshot.h"
#include "tracker.h"
#include "snapimage.h"
#include "tracking.h"

#define SECTION "snapshot  "
#include "log_format.h"

static container_t Snapshots;

int _snapshot_destroy( snapshot_t* p_snapshot );


int snapshot_Init( void )
{
    return container_init( &Snapshots, sizeof( snapshot_t ) );
}

int snapshot_Done( void )
{
    int result = SUCCESS;
    content_t* content;

    log_tr( "Removing all snapshots" );

    while (NULL != (content = container_get_first( &Snapshots ))){
        int status = SUCCESS;
        snapshot_t* p_snapshot = (snapshot_t*)content;

        status = _snapshot_destroy( p_snapshot );
        if (status != SUCCESS){
            log_err_format( "Failed to destroy snapshot [0x%llx]", p_snapshot->id );
            result = status;
        }
    }

    if (result == SUCCESS){
        if (SUCCESS != (result = container_done( &Snapshots ))){
            log_err( "Unable to destroy snapshot container: not empty" );
        };
    }
    return result;
}

int _snapshot_New( dev_t* p_dev, int count, snapshot_t** pp_snapshot )
{
    int result = SUCCESS;
    snapshot_t* p_snapshot = NULL;
    dev_t* snap_set = NULL;

    p_snapshot = (snapshot_t*)content_new( &Snapshots );
    if (NULL == p_snapshot){
        log_err( "Unable to create snapshot: failed to allocate memory for snapshot structure" );
        return -ENOMEM;
    }

    do{
        p_snapshot->id = (unsigned long long)( 0 ) + (unsigned long)(p_snapshot );

        p_snapshot->dev_id_set = NULL;
        p_snapshot->dev_id_set_size = 0;

        {
            size_t buffer_length = sizeof( dev_t ) * count;
            snap_set = (dev_t*)dbg_kzalloc( buffer_length, GFP_KERNEL );
            if (NULL == snap_set){
                log_err( "Unable to create snapshot: faile to allocate memory for snapshot map" );
                result = -ENOMEM;
                break;
            }
            memcpy( snap_set, p_dev, buffer_length );
        }

        p_snapshot->dev_id_set_size = count;
        p_snapshot->dev_id_set = snap_set;

        *pp_snapshot = p_snapshot;
        container_push_back( &Snapshots, &p_snapshot->content );
    } while (false);

    if (result != SUCCESS){
        if (snap_set != NULL){
            dbg_kfree( snap_set );
            snap_set = NULL;
        }

        content_free( &p_snapshot->content );
    }
    return result;
}

int _snapshot_Free( snapshot_t* snapshot )
{
    int result = SUCCESS;

    if (snapshot->dev_id_set != NULL){
        dbg_kfree( snapshot->dev_id_set );
        snapshot->dev_id_set = NULL;
        snapshot->dev_id_set_size = 0;
    }
    return result;
}

int _snapshot_Delete( snapshot_t* p_snapshot )
{
    int result;
    result = _snapshot_Free( p_snapshot );

    if (result == SUCCESS)
        content_free( &p_snapshot->content );
    return result;
}

typedef struct FindBySnapshotId_s
{
    unsigned long long id;
    content_t* pContent;
}FindBySnapshotId_t;

int _FindById_cb( content_t* pContent, void* parameter )
{
    FindBySnapshotId_t* pParam = (FindBySnapshotId_t*)parameter;
    snapshot_t* p_snapshot = (snapshot_t*)pContent;

    if (p_snapshot->id == pParam->id){
        pParam->pContent = pContent;
        return SUCCESS;    //don`t continue
    }
    return ENODATA; //continue
}
/*
* return:
*     SUCCESS if found;
*     ENODATA if not found
*     anything else in error case.
*/
int snapshot_FindById( unsigned long long id, snapshot_t** pp_snapshot )
{
    int result = SUCCESS;
    FindBySnapshotId_t param = {
        .id = id,
        .pContent = NULL
    };

    result = container_enum( &Snapshots, _FindById_cb, &param );

    if (SUCCESS == result){
        *pp_snapshot = (snapshot_t*)param.pContent;
        if ((NULL == param.pContent))
            result = -ENODATA;
    }
    else
        *pp_snapshot = NULL;
    return result;
}


int _snapshot_remove_device( dev_t dev_id )
{
    int result = SUCCESS;
    tracker_t* tracker = NULL;

    result = tracker_find_by_dev_id( dev_id, &tracker );
    if (result != SUCCESS){
        if (result == -ENODEV){
            log_err_dev_t( "Cannot find device by device id=", dev_id );
        }
        else{
            log_err_dev_t( "Failed to find device by device id=", dev_id );
        }
        return SUCCESS;
    }

    if (result != SUCCESS)
        return result;

    tracker_snapshot_id_set(tracker, 0ull);

    log_tr_format( "Device [%d:%d] successfully removed from snapshot", MAJOR( dev_id ), MINOR( dev_id ) );

    return result;
}

int _snapshot_cleanup( snapshot_t* snapshot )
{
    int result = SUCCESS;
    int inx = 0;
    unsigned long long snapshot_id = snapshot->id;

    for (; inx < snapshot->dev_id_set_size; ++inx){
        result = _snapshot_remove_device( snapshot->dev_id_set[inx] );
        if (result != SUCCESS){
            log_err_format( "Failed to remove device [%d:%d] from snapshot", snapshot->dev_id_set[inx] );
        }
    }

    result = _snapshot_Delete( snapshot );
    if (result != SUCCESS){
        log_err_format( "Failed to delete snapshot [0x%llx]", snapshot_id );
    }
    return result;
}

int snapshot_Create( dev_t* dev_id_set, unsigned int dev_id_set_size, unsigned int cbt_block_size_degree, unsigned long long* psnapshot_id )
{
    snapshot_t* snapshot = NULL;
    int result = SUCCESS;
    unsigned int inx = 0;

    log_tr( "Create snapshot for devices:" );
    for (inx = 0; inx < dev_id_set_size; ++inx){
        dev_t dev_id = dev_id_set[inx];
        log_tr_format( "\t%d:%d", MAJOR( dev_id ), MINOR( dev_id ) );
    }
    result = _snapshot_New( dev_id_set, dev_id_set_size, &snapshot );
    if (result != SUCCESS){
        log_err( "Unable to create snapshot: failed to allocate snapshot structure" );
        return result;
    }
    do{
        result = -ENODEV;
        for (inx = 0; inx < snapshot->dev_id_set_size; ++inx)
        {
            dev_t dev_id = snapshot->dev_id_set[inx];

            result = tracking_add(dev_id, cbt_block_size_degree, snapshot->id);
            if (result == -EALREADY)
                result = SUCCESS;
            else if (result != SUCCESS){
                log_err_format( "Unable to create snapshot: failed to add device [%d:%d] to snapshot tracking",
                    MAJOR( dev_id ), MINOR( dev_id ) );
                break;
            }
        }
        if (result != SUCCESS)
            break;


        result = tracker_capture_snapshot( snapshot );
        if (SUCCESS != result){
            log_err_format( "Unable to create snapshot: failed to capture snapshot [0x%llx]", snapshot->id );
            break;
        }

        result = snapimage_create_for( snapshot->dev_id_set, snapshot->dev_id_set_size );
        if (result != SUCCESS){
            log_err( "Unable to create snapshot: failed to create snapshot image devices" );

            tracker_release_snapshot( snapshot );
            break;
        }

        *psnapshot_id = snapshot->id;
        log_tr_format( "Snapshot [0x%llx] was created", snapshot->id );
    } while (false);

    if (SUCCESS != result){
        int res;

        log_tr_format( "Snapshot [0x%llx] cleanup", snapshot->id );

        container_get( &snapshot->content );
        res = _snapshot_cleanup( snapshot );
        if (res != SUCCESS){
            log_err_format( "Failed to perform snapshot [0x%llx] cleanup", snapshot->id );
            container_push_back( &Snapshots, &snapshot->content );
        }
    }
    return result;
}


int _snapshot_destroy( snapshot_t* snapshot )
{
    int result = SUCCESS;
    size_t inx;

    for (inx = 0; inx < snapshot->dev_id_set_size; ++inx){
        /*int res = */snapimage_stop( snapshot->dev_id_set[inx] );
    }

    result = tracker_release_snapshot( snapshot );
    if (result != SUCCESS){
        log_err_format( "Failed to release snapshot [0x%llx]", snapshot->id );
        return result;
    }

    for (inx = 0; inx < snapshot->dev_id_set_size; ++inx){
        /*int res = */snapimage_destroy( snapshot->dev_id_set[inx] );
    }

    return _snapshot_cleanup( snapshot );
}

int snapshot_Destroy( unsigned long long snapshot_id )
{
    int result = SUCCESS;
    snapshot_t* snapshot = NULL;

    log_tr_format( "Destroy snapshot [0x%llx]", snapshot_id );

    result = snapshot_FindById( snapshot_id, &snapshot );
    if (result != SUCCESS){
        log_err_format( "Unable to destroy snapshot [0x%llx]: cannot find snapshot by id", snapshot_id );
        return result;
    }

    container_get( &snapshot->content );
    result = _snapshot_destroy( snapshot );
    if (result != SUCCESS){
        container_push_back( &Snapshots, &snapshot->content );
    }
    return result;
}


