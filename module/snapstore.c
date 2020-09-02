#include "stdafx.h"
#include "snapstore.h"
#include "snapstore_device.h"

#define SECTION "snapstore "
#include "log_format.h"

container_t Snapstore;

bool _snapstore_check_halffill( snapstore_t* snapstore, sector_t* fill_status )
{
    blk_descr_pool_t* pool = NULL;
    if (snapstore->file)
        pool = &snapstore->file->pool;
#ifdef SNAPSTORE_MULTIDEV
    else if (snapstore->multidev)
        pool = &snapstore->multidev->pool;
#endif
    else if (snapstore->mem)
        pool = &snapstore->mem->pool;

    if (pool)
        return blk_descr_pool_check_halffill( pool, snapstore->empty_limit, fill_status );
    else
    return false;
}

void _snapstore_destroy( snapstore_t* snapstore )
{
    sector_t fill_status;

    log_tr_uuid( "Destroy snapstore with id=", (&snapstore->id) );

    _snapstore_check_halffill( snapstore, &fill_status );

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
    container_free( &snapstore->content );
}

void _snapstore_destroy_cb( void* resource )
{
    _snapstore_destroy( (snapstore_t*)resource );
}

int snapstore_init( )
{
    int res = SUCCESS;

    res = container_init( &Snapstore, sizeof( snapstore_t ) );
    if (res != SUCCESS)
        log_err( "Failed to initialize snapstore" );

    return res;
}

void snapstore_done( )
{
    if (SUCCESS != container_done( &Snapstore ))
        log_err( "Unable to perform snapstore cleanup: container is not empty" );
}

int snapstore_create( veeam_uuid_t* id, dev_t snapstore_dev_id, dev_t* dev_id_set, size_t dev_id_set_length )
{
    int res = SUCCESS;
    size_t dev_id_inx;
    snapstore_t* snapstore = NULL;

    if (dev_id_set_length == 0)
        return -EINVAL;

    snapstore = (snapstore_t*)container_new( &Snapstore );
    if (snapstore == NULL)
        return -ENOMEM;

    veeam_uuid_copy( &snapstore->id, id );

    log_tr_uuid( "Create snapstore with id ", (&snapstore->id) );

    snapstore->mem = NULL;
    snapstore->multidev = NULL;
    snapstore->file = NULL;

    snapstore->ctrl_pipe = NULL;
    snapstore->empty_limit = (sector_t)(64 * (1024 * 1024 / SECTOR_SIZE)); //by default value
    snapstore->halffilled = false;
    snapstore->overflowed = false;

    if (snapstore_dev_id == 0){
        log_tr( "Memory snapstore create" );
        // memory buffer selected
        // snapstore_mem_create( size );
    }
#ifdef SNAPSTORE_MULTIDEV
    else if (snapstore_dev_id == 0xFFFFffff){
        snapstore_multidev_t* multidev = NULL;
        res = snapstore_multidev_create( &multidev );
        if (res != SUCCESS){
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
            log_err_uuid( "Failed to create snapstore file for snapstore ", id );
            return res;
        }
        snapstore->file = file;
    }

    shared_resource_init( &snapstore->shared, snapstore, _snapstore_destroy_cb );

    snapstore_get( snapstore );
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

#ifdef SNAPSTORE_MULTIDEV
int snapstore_create_multidev(veeam_uuid_t* id, dev_t* dev_id_set, size_t dev_id_set_length)
{
    int res = SUCCESS;
    size_t dev_id_inx;
    snapstore_t* snapstore = NULL;

    if (dev_id_set_length == 0)
        return -EINVAL;

    snapstore = (snapstore_t*)container_new( &Snapstore );
    if (snapstore == NULL)
        return -ENOMEM;

    veeam_uuid_copy( &snapstore->id, id );

    log_tr_uuid( "Create snapstore with id ", (&snapstore->id) );

    snapstore->mem = NULL;
    snapstore->file = NULL;
    snapstore->multidev = NULL;

    snapstore->ctrl_pipe = NULL;
    snapstore->empty_limit = (sector_t)(64 * (1024 * 1024 / SECTOR_SIZE)); //by default value
    snapstore->halffilled = false;
    snapstore->overflowed = false;

    {
        snapstore_multidev_t* multidev = NULL;
        res = snapstore_multidev_create( &multidev );
        if (res != SUCCESS){
            log_err_uuid( "Failed to create snapstore file for snapstore ", id );
            return res;
        }
        snapstore->multidev = multidev;
    }

    shared_resource_init( &snapstore->shared, snapstore, _snapstore_destroy_cb );

    snapstore_get( snapstore );
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


int snapstore_cleanup( veeam_uuid_t* id, stream_size_t* filled_bytes )
{
    int res;
    sector_t filled;
    res = snapstore_check_halffill( id, &filled );
    if (res == SUCCESS){
        *filled_bytes = sector_to_streamsize( filled );

        log_tr_format( "Snapstore fill size: %lld MiB", (*filled_bytes >> 20) );
    }else{
        *filled_bytes = -1;
        log_err( "Failed to obtain snapstore data filled size" );
    }

    return snapstore_device_cleanup( id );
}

snapstore_t* _snapstore_find( veeam_uuid_t* id )
{
    snapstore_t* result = NULL;
    content_t* content;

    CONTAINER_FOREACH_BEGIN( Snapstore, content )
    {
        snapstore_t* snapstore = (snapstore_t*)content;
        if (veeam_uuid_equal( &snapstore->id, id )){
            result = snapstore;
            break;
        }
    }
    CONTAINER_FOREACH_END( Snapstore );

    return result;
}

int snapstore_stretch_initiate( veeam_uuid_t* unique_id, ctrl_pipe_t* ctrl_pipe, sector_t empty_limit )
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

int snapstore_add_memory( veeam_uuid_t* id, unsigned long long sz )
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
#ifdef SNAPSTORE_MULTIDEV
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
        size_t available_blocks = (size_t)(sz >> (SNAPSTORE_BLK_SHIFT + SECTOR_SHIFT));
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

#ifdef SNAPDATA_ZEROED

int zerosectors_add_ranges( rangevector_t* zero_sectors, page_array_t* ranges, size_t ranges_cnt )
{
    if ((ranges == NULL) || (ranges_cnt == 0))
        return -EINVAL;

    if (get_zerosnapdata( )){
        unsigned int inx = 0;

        for (inx = 0; inx < ranges_cnt; ++inx){
            int res = SUCCESS;
            
            range_t range;
            struct ioctl_range_s* ioctl_range = (struct ioctl_range_s*)page_get_element( ranges, inx, sizeof( struct ioctl_range_s ) );

            range.ofs = sector_from_streamsize( ioctl_range->left );
            range.cnt = sector_from_streamsize( ioctl_range->right ) - range.ofs;

            res = rangevector_add( zero_sectors, &range );
            if (res != SUCCESS){
                log_err( "Failed to add range to zero sectors" );
    return res;
}
        }
        rangevector_sort( zero_sectors );
    }
    return SUCCESS;
}

#endif //SNAPDATA_ZEROED

int snapstore_add_file( veeam_uuid_t* id, page_array_t* ranges, size_t ranges_cnt )
{
    int res = SUCCESS;
    snapstore_t* snapstore = NULL;
    sector_t current_blk_size = 0;

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

    {
        size_t inx;
        rangelist_t blk_rangelist;
        rangelist_init( &blk_rangelist );

        for (inx = 0; inx < ranges_cnt; ++inx){
            size_t blocks_count = 0;
            sector_t range_offset = 0;

            range_t range;
            struct ioctl_range_s* ioctl_range = (struct ioctl_range_s*)page_get_element( ranges, inx, sizeof( struct ioctl_range_s ) );

            range.ofs = sector_from_streamsize( ioctl_range->left );
            range.cnt = sector_from_streamsize( ioctl_range->right ) - range.ofs;

            //log_tr_range( "range=", range );

            while (range_offset < range.cnt){
                range_t rg;

                rg.ofs = range.ofs + range_offset;
                rg.cnt = min_t( sector_t, (range.cnt - range_offset), (SNAPSTORE_BLK_SIZE - current_blk_size) );

                range_offset += rg.cnt;

                //log_tr_range( "add rg=", rg );

                res = rangelist_add( &blk_rangelist, &rg );
                if (res != SUCCESS){
                    log_err( "Unable to add file to snapstore: cannot add range to rangelist" );
                    break;
                }
                current_blk_size += rg.cnt;

                if (current_blk_size == SNAPSTORE_BLK_SIZE){//allocate  block
                    res = blk_descr_file_pool_add( &snapstore->file->pool, &blk_rangelist );
                    if (res != SUCCESS){
                        log_err( "Unable to add file to snapstore: cannot initialize new block" );
                        break;
                    }

                    snapstore->halffilled = false;

                    current_blk_size = 0;
                    rangelist_init( &blk_rangelist );
                    ++blocks_count;
                }
            }
            if (res != SUCCESS)
                break;

            //log_traceln_sz( "blocks_count=", blocks_count );
        }
    }
    if ((res == SUCCESS) && (current_blk_size != 0))
        log_warn( "Snapstore portion was not ordered by Copy-on-Write block size" );
    
#ifdef SNAPDATA_ZEROED
    if ((res == SUCCESS) && (snapstore->file != NULL)){
        snapstore_device_t* snapstore_device = snapstore_device_find_by_dev_id( snapstore->file->blk_dev_id );
        if (snapstore_device != NULL){
            res = zerosectors_add_ranges( &snapstore_device->zero_sectors, ranges, ranges_cnt );
            if (res != SUCCESS){
                log_err( "Failed to add file ranges to zeroed sectors set" );
            }
        }
    }
#endif

    return res;
}

#ifdef SNAPSTORE_MULTIDEV
int snapstore_add_multidev(veeam_uuid_t* id, dev_t dev_id, page_array_t* ranges, size_t ranges_cnt)
{
    int res = SUCCESS;
    snapstore_t* snapstore = NULL;
    sector_t current_blk_size = 0;

    log_tr_format( "Snapstore add %ld ranges for device [%d:%d]", ranges_cnt, MAJOR(dev_id), MINOR(dev_id) );

    if ((ranges_cnt == 0) || (ranges == NULL))
        return -EINVAL;

    snapstore = _snapstore_find( id );
    if (snapstore == NULL){
        log_err_uuid( "Unable to add file to snapstore: cannot find snapstore by id ", id );
        return -ENODATA;
            }

    if (snapstore->multidev == NULL){
        log_err( "Unable to add file to multidevice snapstore: multidevice snapstore was not initialized" );
        return -EFAULT;
        }

    {
        size_t inx;
        rangelist_ex_t blk_rangelist;
        rangelist_ex_init( &blk_rangelist );

        for (inx = 0; inx < ranges_cnt; ++inx){
            size_t blocks_count = 0;
            sector_t range_offset = 0;

            range_t range;
            struct ioctl_range_s* data = (struct ioctl_range_s*)page_get_element( ranges, inx, sizeof( struct ioctl_range_s ) );

            range.ofs = sector_from_streamsize( data->left );
            range.cnt = sector_from_streamsize( data->right ) - range.ofs;

            //log_tr_format( "range=%lld:%lld", range.ofs, range.cnt );

            while (range_offset < range.cnt){
                range_t rg;
                void* extension = NULL;
                rg.ofs = range.ofs + range_offset;
                rg.cnt = min_t( sector_t, (range.cnt - range_offset), (SNAPSTORE_BLK_SIZE - current_blk_size) );

                range_offset += rg.cnt;

                //log_tr_range( "add rg=", rg );
                extension = (void*)snapstore_multidev_get_device( snapstore->multidev, dev_id );
                if (NULL == extension){
                    log_err_format( "Cannot find or open device [%d:%d] for multidevice snapstore", MAJOR( dev_id ), MINOR( dev_id ) );
                    res = -ENODEV;
                    break;
    }

                res = rangelist_ex_add( &blk_rangelist, &rg, extension );
                if (res != SUCCESS){
                    log_err( "Unable to add file to snapstore: failed to add range to rangelist" );
                    break;
                }
                current_blk_size += rg.cnt;

                if (current_blk_size == SNAPSTORE_BLK_SIZE){//allocate  block
                    res = blk_descr_multidev_pool_add( &snapstore->multidev->pool, &blk_rangelist );
                    if (res != SUCCESS){
                        log_err( "Unable to add file to snapstore: failed to initialize new block" );
                        break;
                    }

                    snapstore->halffilled = false;

                    current_blk_size = 0;
                    rangelist_ex_init( &blk_rangelist );
                    ++blocks_count;
                }
            }
            if (res != SUCCESS)
                break;

            //log_traceln_sz( "blocks_count=", blocks_count );
        }
    }
    if ((res == SUCCESS) && (current_blk_size != 0))
        log_warn( "Snapstore portion was not ordered by Copy-on-Write block size" );

#ifdef SNAPDATA_ZEROED
//  zeroing algorithm is not supported for multidevice snapstore
//
//     if ((res == SUCCESS) && (snapstore->multidev != NULL)){
//         snapstore_device_t* snapstore_device = snapstore_device_find_by_dev_id( snapstore->multidev->blk_dev_id );
//         if (snapstore_device != NULL){
//             res = zerosectors_add_ranges( &snapstore_device->zero_sectors, ranges, ranges_cnt );
//             if (res != SUCCESS){
//                 log_err( "Failed to add file ranges to zeroed sectors set" );
//             }
//         }
//     }
#endif

    return res;
}
#endif

void snapstore_order_border( range_t* in, range_t* out )
{
    range_t unorder;

    unorder.ofs = in->ofs & SNAPSTORE_BLK_MASK;
    out->ofs = in->ofs & ~SNAPSTORE_BLK_MASK;
    out->cnt = in->cnt + unorder.ofs;

    unorder.cnt = out->cnt & SNAPSTORE_BLK_MASK;
    if (unorder.cnt != 0)
        out->cnt += (SNAPSTORE_BLK_SIZE - unorder.cnt);
}

blk_descr_unify_t* snapstore_get_empty_block( snapstore_t* snapstore )
{
    blk_descr_unify_t* result = NULL;

    if (snapstore->overflowed)
        return NULL;

    if (snapstore->file != NULL)
        result = (blk_descr_unify_t*)blk_descr_file_pool_take( &snapstore->file->pool );
    else if (snapstore->multidev != NULL)
        result = (blk_descr_unify_t*)blk_descr_multidev_pool_take( &snapstore->multidev->pool );
    else if (snapstore->mem != NULL)
        result = (blk_descr_unify_t*)blk_descr_mem_pool_take( &snapstore->mem->pool );

    if (NULL == result){
        if (snapstore->ctrl_pipe){
            sector_t fill_status;
            _snapstore_check_halffill( snapstore, &fill_status );
            ctrl_pipe_request_overflow( snapstore->ctrl_pipe, -EINVAL, sector_to_streamsize( fill_status ) );
        }
        snapstore->overflowed = true;
    }

    return result;
}

int snapstore_check_halffill( veeam_uuid_t* unique_id, sector_t* fill_status )
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
                ctrl_pipe_request_halffill( snapstore->ctrl_pipe, sector_to_streamsize( fill_status ) );
            }
        }
    }

    if (snapstore->file)
        res = blk_deferred_request_store_file( snapstore->file->blk_dev, dio_copy_req );
#ifdef SNAPSTORE_MULTIDEV
    else if (snapstore->multidev)
        res = blk_deferred_request_store_multidev( dio_copy_req );
#endif
    else if (snapstore->mem)
        res = blk_deffered_request_store_mem( dio_copy_req );
    else
        res = -EINVAL;

    return res;
}

int snapstore_redirect_read( blk_redirect_bio_endio_t* rq_endio, snapstore_t* snapstore, blk_descr_unify_t* blk_descr_ptr, sector_t target_pos, sector_t rq_ofs, sector_t rq_count )
{
    int res = SUCCESS;
    sector_t current_ofs = 0;
    sector_t block_ofs = target_pos & SNAPSTORE_BLK_MASK;


    if (snapstore->file){
        range_t* rg;
        blk_descr_file_t* blk_descr = (blk_descr_file_t*)blk_descr_ptr;


        RANGELIST_FOREACH_BEGIN( blk_descr->rangelist, rg )
        {
            if (current_ofs >= rq_count)
                break;

            if (rg->cnt > block_ofs)//read first portion from block
            {
                sector_t pos = rg->ofs + block_ofs;
                sector_t len = min_t( sector_t, (rg->cnt - block_ofs), (rq_count - current_ofs) );

                res = blk_dev_redirect_part( rq_endio, READ, snapstore->file->blk_dev, pos, rq_ofs + current_ofs, len );

                if (res != SUCCESS){
                    log_err_sect( "Failed to read from snapstore file. Sector #", pos );
                    break;
                }

                current_ofs += len;
                block_ofs = 0;
            }
            else{
                block_ofs -= rg->cnt;
            }
        }
        RANGELIST_FOREACH_END( );

    }
#ifdef SNAPSTORE_MULTIDEV
    else if (snapstore->multidev) {
        range_t* rg;
        void** p_extentsion;
        blk_descr_multidev_t* blk_descr = (blk_descr_multidev_t*)blk_descr_ptr;

        RANGELIST_EX_FOREACH_BEGIN( blk_descr->rangelist, rg, p_extentsion )
        {
            struct block_device*  blk_dev = (struct block_device*)(*p_extentsion);
            if (current_ofs >= rq_count)
                break;

            if (rg->cnt > block_ofs)//read first portion from block
            {
                sector_t pos = rg->ofs + block_ofs;
                sector_t len = min_t( sector_t, (rg->cnt - block_ofs), (rq_count - current_ofs) );

                res = blk_dev_redirect_part( rq_endio, READ, blk_dev, pos, rq_ofs + current_ofs, len );

                if (res != SUCCESS){
                    log_err_sect( "Failed to read from snapstore file. Sector #", pos );
                    break;
                }

                current_ofs += len;
                block_ofs = 0;
            }
            else{
                block_ofs -= rg->cnt;
            }
        }
        RANGELIST_EX_FOREACH_END( );
    }
#endif
    else if (snapstore->mem){
        blk_descr_mem_t* blk_descr = (blk_descr_mem_t*)blk_descr_ptr;

        res = blk_dev_redirect_memcpy_part( rq_endio, READ, blk_descr->buff + sector_to_size( block_ofs ), rq_ofs, rq_count );
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

int snapstore_redirect_write( blk_redirect_bio_endio_t* rq_endio, snapstore_t* snapstore, blk_descr_unify_t* blk_descr_ptr, sector_t target_pos, sector_t rq_ofs, sector_t rq_count )
{
    int res = SUCCESS;
    sector_t current_ofs = 0;
    sector_t block_ofs = target_pos & SNAPSTORE_BLK_MASK;

    BUG_ON( NULL == rq_endio );
    BUG_ON( NULL == snapstore );

    if (snapstore->file){
        range_t* rg;
        blk_descr_file_t* blk_descr = (blk_descr_file_t*)blk_descr_ptr;


        RANGELIST_FOREACH_BEGIN( blk_descr->rangelist, rg )
        {
            if (current_ofs >= rq_count)
                break;

            if (rg->cnt > block_ofs)//read first portion from block
            {
                sector_t pos = rg->ofs + block_ofs;
                sector_t len = min_t( sector_t, (rg->cnt - block_ofs), (rq_count - current_ofs) );

                res = blk_dev_redirect_part( rq_endio, WRITE, snapstore->file->blk_dev, pos, rq_ofs + current_ofs, len );

                if (res != SUCCESS){
                    log_err_sect( "Failed to write to snapstore file. Sector #", pos );
                    break;
                }

                current_ofs += len;
                block_ofs = 0;
            }
            else{
                block_ofs -= rg->cnt;
            }
        }
        RANGELIST_FOREACH_END( );

    }
#ifdef SNAPSTORE_MULTIDEV
    else if (snapstore->multidev) {
        range_t* rg;
        void** p_extension;
        blk_descr_file_t* blk_descr = (blk_descr_file_t*)blk_descr_ptr;


        RANGELIST_EX_FOREACH_BEGIN( blk_descr->rangelist, rg, p_extension )
        {
            struct block_device*  blk_dev = (struct block_device*)(*p_extension);

            if (current_ofs >= rq_count)
                break;

            if (rg->cnt > block_ofs)//read first portion from block
            {
                sector_t pos = rg->ofs + block_ofs;
                sector_t len = min_t( sector_t, (rg->cnt - block_ofs), (rq_count - current_ofs) );

                res = blk_dev_redirect_part( rq_endio, WRITE, blk_dev, pos, rq_ofs + current_ofs, len );

                if (res != SUCCESS){
                    log_err_sect( "Failed to write to snapstore file. Sector #", pos );
                    break;
                }

                current_ofs += len;
                block_ofs = 0;
            }
            else{
                block_ofs -= rg->cnt;
            }
        }
        RANGELIST_EX_FOREACH_END( );

    }
#endif
    else if (snapstore->mem){
        blk_descr_mem_t* blk_descr = (blk_descr_mem_t*)blk_descr_ptr;

        res = blk_dev_redirect_memcpy_part( rq_endio, WRITE, blk_descr->buff + sector_to_size( block_ofs ), rq_ofs, rq_count );
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


