#include "stdafx.h"
#include "snapstore_device.h"
#include "snapstore.h"
#include "blk_util.h"

#define SECTION "snapstore "
#include "log_format.h"

container_t SnapstoreDevices;

static inline void _snapstore_device_descr_write_lock( snapstore_device_t* snapstore_device )
{
//    down_write( &snapstore_device->store_block_map_locker );
    mutex_lock( &snapstore_device->store_block_map_locker );
}
static inline void _snapstore_device_descr_write_unlock( snapstore_device_t* snapstore_device )
{
//    up_write( &snapstore_device->store_block_map_locker );
    mutex_unlock( &snapstore_device->store_block_map_locker );
}


int snapstore_device_init( void )
{
    int res = SUCCESS;

    res = container_init( &SnapstoreDevices, sizeof( snapstore_device_t ) );
    if (res != SUCCESS)
        return res;


    return res;
}

void snapstore_device_done( void )
{
    if (SUCCESS != container_done( &SnapstoreDevices )){
        snapstore_device_t* snapstore_device;

        log_err( "Cleanup snapstore devices" );

        while (NULL != (snapstore_device = (snapstore_device_t*)container_get_first( &SnapstoreDevices ))){
            snapstore_device_put_resource( snapstore_device );
        }
    }
}

snapstore_device_t* snapstore_device_find_by_dev_id( dev_t dev_id )
{
    content_t* content;
    snapstore_device_t* result = NULL;

    CONTAINER_FOREACH_BEGIN( SnapstoreDevices, content )
    {
        snapstore_device_t* snapstore_device = (snapstore_device_t*)(content);

        if (dev_id == snapstore_device->dev_id){
            result = snapstore_device;
            //_container_del( &SnapstoreDevices, content );
            break;
        }
    }
    CONTAINER_FOREACH_END( SnapstoreDevices );

    return result;
}

snapstore_device_t* _snapstore_device_get_by_snapstore_id( veeam_uuid_t* id )
{
    content_t* content;
    snapstore_device_t* result = NULL;

    CONTAINER_FOREACH_BEGIN( SnapstoreDevices, content )
    {
        snapstore_device_t* snapstore_device = (snapstore_device_t*)(content);

        if (veeam_uuid_equal( id, &snapstore_device->snapstore->id )){
            result = snapstore_device;
            _container_del( &SnapstoreDevices, content );
            //found
            break;
        }
    }
    CONTAINER_FOREACH_END( SnapstoreDevices );
    return result;
}


void _snapstore_device_destroy( snapstore_device_t* snapstore_device )
{
    log_tr("Destroy snapstore device");

    blk_descr_array_done( &snapstore_device->store_block_map );

    if (snapstore_device->orig_blk_dev != NULL)
        blk_dev_close( snapstore_device->orig_blk_dev );

#ifdef SNAPDATA_ZEROED
    rangevector_done( &snapstore_device->zero_sectors );
#endif
    if (snapstore_device->snapstore){
        log_tr_uuid("Snapstore uuid ", (&snapstore_device->snapstore->id));

        snapstore_put( snapstore_device->snapstore );
        snapstore_device->snapstore = NULL;
    }
    content_free( &snapstore_device->content );
}

void snapstore_device_free_cb( void* resource )
{
    snapstore_device_t* snapstore_device = (snapstore_device_t*)resource;

    _snapstore_device_destroy( snapstore_device );
}

int snapstore_device_cleanup( veeam_uuid_t* id )
{
    int result = SUCCESS;
    snapstore_device_t* snapstore_device = NULL;

    while (NULL != (snapstore_device = _snapstore_device_get_by_snapstore_id( id ))){
        log_tr_dev_t( "Cleanup snapstore device for device ", snapstore_device->dev_id );

        snapstore_device_put_resource( snapstore_device );
    }
    return result;
}

int snapstore_device_create( dev_t dev_id, snapstore_t* snapstore )
{
    int res = SUCCESS;
    snapstore_device_t* snapstore_device;

    snapstore_device = (snapstore_device_t*)content_new( &SnapstoreDevices );
    if (NULL == snapstore_device)
        return -ENOMEM;

    snapstore_device->dev_id = dev_id;

    res = blk_dev_open( dev_id, &snapstore_device->orig_blk_dev );
    if (res != SUCCESS){
        log_err_dev_t( "Unable to create snapstore device: failed to open original device ", dev_id );
        return res;
    }

    shared_resource_init( &snapstore_device->shared, snapstore_device, snapstore_device_free_cb );

    snapstore_device->snapstore = NULL;
    snapstore_device->err_code = SUCCESS;
    snapstore_device->corrupted = false;
    atomic_set( &snapstore_device->req_failed_cnt, 0 );

    //init_rwsem( &snapstore_device->store_block_map_locker );
    mutex_init(&snapstore_device->store_block_map_locker);

#ifdef SNAPDATA_ZEROED
    rangevector_init(&snapstore_device->zero_sectors, true);
#endif

    while (1){ //failover with snapstore block size increment
        blk_descr_array_index_t blocks_count;

        sector_t sect_cnt = blk_dev_get_capacity(snapstore_device->orig_blk_dev);
        if (sect_cnt & SNAPSTORE_BLK_MASK)
            sect_cnt += SNAPSTORE_BLK_SIZE;
        blocks_count = (blk_descr_array_index_t)(sect_cnt >> SNAPSTORE_BLK_SHIFT);

        res = blk_descr_array_init(&snapstore_device->store_block_map, 0, blocks_count);
        if (res == SUCCESS)
            break;

        log_err_format("Failed to initialize block description array, %lu blocks needed", blocks_count);

        res = inc_snapstore_block_size_pow();
        if (res != SUCCESS){
            log_err("Cannot increment block size");
            break;
        }

        log_warn_format("Snapstore block size increased to %lld sectors", SNAPSTORE_BLK_SIZE);
    }
    if (res != SUCCESS){
        log_err("Unable to create snapstore device");
        _snapstore_device_destroy(snapstore_device);
        return res;
    }

    snapstore_device->snapstore = snapstore_get(snapstore);

    container_push_back(&SnapstoreDevices, &snapstore_device->content);
    snapstore_device_get_resource(snapstore_device);

    return SUCCESS;
}

bool _snapstore_device_is_block_stored( snapstore_device_t* snapstore_device, blk_descr_array_index_t block_index )
{
    blk_descr_array_el_t blk_descr;
    int res = blk_descr_array_get( &snapstore_device->store_block_map, block_index, &blk_descr );
    if (res == SUCCESS)
        return true;
    if (res == -ENODATA)
        return false;

    log_err_ld( "Snapstore device error: failed to get block description for block #", block_index );
    return false;
}

int snapstore_device_add_request( snapstore_device_t* snapstore_device, blk_descr_array_index_t block_index, blk_deferred_request_t** dio_copy_req )
{
    int res = SUCCESS;
    blk_descr_unify_t* blk_descr = NULL;
    blk_deferred_t* dio = NULL;
    bool req_new = false;

    blk_descr = snapstore_get_empty_block( snapstore_device->snapstore );
    if (blk_descr == NULL){
        log_err( "Unable to add block to defer IO request: failed to allocate next block" );
        return -ENODATA;
    }

    res = blk_descr_array_set( &snapstore_device->store_block_map, block_index, blk_descr );
    if (res != SUCCESS){
        //blk_descr_write_unlock( blk_descr );
        log_err_d( "Unable to add block to defer IO request: failed to set block descriptor to descriptors array. errno=", res );
        return res;
    }

    if (*dio_copy_req == NULL){
        *dio_copy_req = blk_deferred_request_new( );
        if (*dio_copy_req == NULL){
            log_err( "Unable to add block to defer IO request: failed to allocate defer IO request" );
            return -ENOMEM;

        }
        req_new = true;
    }

    do{
        dio = blk_deferred_alloc( block_index, blk_descr );
        if (dio == NULL){
            log_err( "Unabled to add block to defer IO request: failed to allocate defer IO" );
            res = -ENOMEM;
            break;
        }

        res = blk_deferred_request_add( *dio_copy_req, dio );
        if (res != SUCCESS){
            log_err( "Unable to add block to defer IO request: failed to add defer IO to request" );
        }
    } while (false);

    if (res != SUCCESS){
        if (dio != NULL){
            blk_deferred_free( dio );
            dio = NULL;
        }
        if (req_new){
            blk_deferred_request_free( *dio_copy_req );
            *dio_copy_req = NULL;
        }
    }

    return res;
}

int snapstore_device_prepare_requests( snapstore_device_t* snapstore_device, range_t* copy_range, blk_deferred_request_t** dio_copy_req )
{
    int res = SUCCESS;
    blk_descr_array_index_t inx = 0;
    blk_descr_array_index_t first = (blk_descr_array_index_t)(copy_range->ofs >> SNAPSTORE_BLK_SHIFT);
    blk_descr_array_index_t last = (blk_descr_array_index_t)((copy_range->ofs + copy_range->cnt - 1) >> SNAPSTORE_BLK_SHIFT);

    for (inx = first; inx <= last; inx++){
        if (_snapstore_device_is_block_stored( snapstore_device, inx ))
        {
            //log_tr_sz( "Already stored block # ", inx );
        }else{

            res = snapstore_device_add_request( snapstore_device, inx, dio_copy_req );
            if ( res != SUCCESS){
                log_err_d( "Failed to create copy defer IO request. errno=", res );
                break;
            }
        }
    }
    if (res != SUCCESS){
        snapstore_device_set_corrupted( snapstore_device, res );
    }

    return res;
}

int snapstore_device_store( snapstore_device_t* snapstore_device, blk_deferred_request_t* dio_copy_req )
{
    int res = snapstore_request_store( snapstore_device->snapstore, dio_copy_req );
    if (res != SUCCESS)
        snapstore_device_set_corrupted( snapstore_device, res );

    return res;
}

int snapstore_device_read( snapstore_device_t* snapstore_device, blk_redirect_bio_endio_t* rq_endio )
{
    int res = SUCCESS;

    blk_descr_array_index_t block_index;
    blk_descr_array_index_t block_index_last;
    blk_descr_array_index_t block_index_first;

    sector_t blk_ofs_start = 0;         //device range start
    sector_t blk_ofs_count = 0;         //device range length

    range_t rq_range;

#ifdef SNAPDATA_ZEROED
    rangevector_t* zero_sectors = NULL;
    if (get_zerosnapdata( ))
        zero_sectors = &snapstore_device->zero_sectors;
#endif //SNAPDATA_ZEROED

    if (snapstore_device_is_corrupted( snapstore_device ))
        return -ENODATA;

    rq_range.cnt = sector_from_size( bio_bi_size( rq_endio->bio ) );
    rq_range.ofs = bio_bi_sector( rq_endio->bio );

    if (!bio_has_data( rq_endio->bio )){
        log_warn_sz( "Empty bio was found during reading from snapstore device. flags=", rq_endio->bio->bi_flags );

        blk_redirect_complete( rq_endio, SUCCESS );
        return SUCCESS;
    }

    block_index_first = (blk_descr_array_index_t)(rq_range.ofs >> SNAPSTORE_BLK_SHIFT);
    block_index_last = (blk_descr_array_index_t)((rq_range.ofs + rq_range.cnt - 1) >> SNAPSTORE_BLK_SHIFT);

    _snapstore_device_descr_write_lock( snapstore_device );
    for (block_index = block_index_first; block_index <= block_index_last; ++block_index){
        int status;
        blk_descr_unify_t* blk_descr = NULL;

        blk_ofs_count = min_t( sector_t,
            (((sector_t)(block_index + 1)) << SNAPSTORE_BLK_SHIFT) - (rq_range.ofs + blk_ofs_start),
            rq_range.cnt - blk_ofs_start );

        status = blk_descr_array_get( &snapstore_device->store_block_map, block_index, &blk_descr );
        if (SUCCESS != status){
            if (-ENODATA == status)
                blk_descr = NULL;
            else{
                res = status;
                log_err( "Unable to read from snapstore device: failed to get snapstore block" );
                break;
            }
        }
        if (blk_descr ){
            //push snapstore read
            res = snapstore_redirect_read( rq_endio, snapstore_device->snapstore, blk_descr, rq_range.ofs + blk_ofs_start, blk_ofs_start, blk_ofs_count );
            if (res != SUCCESS){
                log_err( "Failed to read from snapstore device" );
                break;
            }
        }
        else{

#ifdef SNAPDATA_ZEROED
            //device read with zeroing
            if (zero_sectors)
                res = blk_dev_redirect_read_zeroed( rq_endio, snapstore_device->orig_blk_dev, rq_range.ofs, blk_ofs_start, blk_ofs_count, zero_sectors );
            else
#endif
            res = blk_dev_redirect_part( rq_endio, READ, snapstore_device->orig_blk_dev, rq_range.ofs + blk_ofs_start, blk_ofs_start, blk_ofs_count );

            if (res != SUCCESS){
                log_err_dev_t( "Failed to redirect read request to the original device ", snapstore_device->dev_id );
                break;
            }
        }

        blk_ofs_start += blk_ofs_count;
    }

    if (res == SUCCESS){
        if (atomic64_read( &rq_endio->bio_endio_count ) > 0ll) //async direct access needed
            blk_dev_redirect_submit( rq_endio );
        else
            blk_redirect_complete( rq_endio, res );
    }
    else{
        log_err_d( "Failed to read from snapstore device. errno=", res );
        log_err_format( "Position %lld sector, length %lld sectors", rq_range.ofs, rq_range.cnt );
    }
    _snapstore_device_descr_write_unlock(snapstore_device);

    return res;
}

int _snapstore_device_copy_on_write( snapstore_device_t* snapstore_device, range_t* rq_range )
{
    int res = SUCCESS;
    blk_deferred_request_t* dio_copy_req = NULL;

    _snapstore_device_descr_read_lock( snapstore_device );
    do{
        res = snapstore_device_prepare_requests( snapstore_device, rq_range, &dio_copy_req );
        if (res != SUCCESS){
            log_err_d( "Failed to create defer IO request for range. errno=", res );
            break;
        }

        if (NULL == dio_copy_req)
            break;//nothing to copy

        res = blk_deferred_request_read_original( snapstore_device->orig_blk_dev, dio_copy_req );
        if (res != SUCCESS){
            log_err_d( "Failed to read data from the original device. errno=", res );
            break;
        }
        res = snapstore_device_store( snapstore_device, dio_copy_req );
        if (res != SUCCESS){
            log_err_d( "Failed to write data to snapstore. errno=", res );
            break;
        }
    } while (false);
    _snapstore_device_descr_read_unlock( snapstore_device );

    if (dio_copy_req){
        if (res == -EDEADLK)
            blk_deferred_request_deadlocked( dio_copy_req );
        else
            blk_deferred_request_free( dio_copy_req );
    }

    return res;
}


int snapstore_device_write( snapstore_device_t* snapstore_device, blk_redirect_bio_endio_t* rq_endio )
{
    int res = SUCCESS;

    blk_descr_array_index_t block_index;
    blk_descr_array_index_t block_index_last;
    blk_descr_array_index_t block_index_first;

    sector_t blk_ofs_start = 0;         //device range start
    sector_t blk_ofs_count = 0;         //device range length

    range_t rq_range;

    BUG_ON( NULL == snapstore_device );
    BUG_ON( NULL == rq_endio );
    BUG_ON( NULL == rq_endio->bio );

    if (snapstore_device_is_corrupted( snapstore_device ))
        return -ENODATA;

    rq_range.cnt = sector_from_size( bio_bi_size( rq_endio->bio ) );
    rq_range.ofs = bio_bi_sector( rq_endio->bio );

    if (!bio_has_data( rq_endio->bio )){
        log_warn_sz( "Empty bio was found during reading from snapstore device. flags=", rq_endio->bio->bi_flags );

        blk_redirect_complete( rq_endio, SUCCESS );
        return SUCCESS;
    }

    // do copy to snapstore previously
    res = _snapstore_device_copy_on_write( snapstore_device, &rq_range );

    block_index_first = (blk_descr_array_index_t)(rq_range.ofs >> SNAPSTORE_BLK_SHIFT);
    block_index_last = (blk_descr_array_index_t)((rq_range.ofs + rq_range.cnt - 1) >> SNAPSTORE_BLK_SHIFT);

    _snapstore_device_descr_write_lock(snapstore_device);
    for (block_index = block_index_first; block_index <= block_index_last; ++block_index){
        int status;
        blk_descr_unify_t* blk_descr = NULL;

        blk_ofs_count = min_t( sector_t,
            (((sector_t)(block_index + 1)) << SNAPSTORE_BLK_SHIFT) - (rq_range.ofs + blk_ofs_start),
            rq_range.cnt - blk_ofs_start );

        status = blk_descr_array_get( &snapstore_device->store_block_map, block_index, &blk_descr );
        if (SUCCESS != status){
            if (-ENODATA == status)
                blk_descr = NULL;
            else{
                res = status;
                log_err( "Unable to write from snapstore device: failed to get snapstore block descriptor" );
                break;
            }
        }
        if (blk_descr == NULL){
            log_err( "Unable to write from snapstore device: invalid snapstore block descriptor" );
            res = -EIO;
            break;
        }

        res = snapstore_redirect_write( rq_endio, snapstore_device->snapstore, blk_descr, rq_range.ofs + blk_ofs_start, blk_ofs_start, blk_ofs_count );
        if (res != SUCCESS){
            log_err( "Unable to write from snapstore device: failed to redirect write request to snapstore" );
            break;
        }

        blk_ofs_start += blk_ofs_count;
    }
    if (res == SUCCESS){
        if (atomic64_read( &rq_endio->bio_endio_count ) > 0){ //async direct access needed
            blk_dev_redirect_submit( rq_endio );
        }
        else{
            blk_redirect_complete( rq_endio, res );
        }
    }
    else{
        log_err_d( "Failed to write from snapstore device. errno=", res );
        log_err_format( "Position %lld sector, length %lld sectors", rq_range.ofs, rq_range.cnt );

        snapstore_device_set_corrupted( snapstore_device, res );
    }
    _snapstore_device_descr_write_unlock(snapstore_device);
    return res;
}

bool snapstore_device_is_corrupted( snapstore_device_t* snapstore_device )
{
    if (snapstore_device == NULL)
        return true;

    if (snapstore_device->corrupted){
        if (0 == atomic_read( &snapstore_device->req_failed_cnt )){
            log_err_dev_t( "Snapshot device is corrupted for ", snapstore_device->dev_id );
        }
        atomic_inc( &snapstore_device->req_failed_cnt );
        return true;
    }

    return false;
}

void snapstore_device_set_corrupted( snapstore_device_t* snapstore_device, int err_code )
{
    if (!snapstore_device->corrupted){
        atomic_set( &snapstore_device->req_failed_cnt, 0 );
        snapstore_device->corrupted = true;
        snapstore_device->err_code = err_code;

        log_err_dev_t( "Set snapshot device is corrupted for ", snapstore_device->dev_id );
    }
}

void snapstore_device_print_state( snapstore_device_t* snapstore_device )
{
    log_tr( "" );
    log_tr_dev_t( "Snapstore device state for device ", snapstore_device->dev_id );

    if (snapstore_device->corrupted){
        log_tr( "Corrupted");
        log_tr_d( "Failed request count: ", atomic_read( &snapstore_device->req_failed_cnt ) );
    }
}

int snapstore_device_errno( dev_t dev_id, int* p_err_code )
{
    snapstore_device_t* snapstore_device = snapstore_device_find_by_dev_id( dev_id );
    if (snapstore_device == NULL)
        return -ENODATA;

    *p_err_code = snapstore_device->err_code;
    return SUCCESS;
}
