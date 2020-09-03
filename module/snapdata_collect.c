#include "stdafx.h"
#include "snapdata_collect.h"
#include "blk_util.h"

#define SECTION "snapdatact"
#include "log_format.h"

static container_sl_t SnapdataCollectors;


int _collector_init( snapdata_collector_t* collector, dev_t dev_id, void* MagicUserBuff, size_t MagicLength );
void _collector_free( snapdata_collector_t* collector );


int snapdata_collect_Init( void )
{
    container_sl_init( &SnapdataCollectors, sizeof( snapdata_collector_t ));
    return SUCCESS;
}


int snapdata_collect_Done( void )
{
    int res;
    content_sl_t* content = NULL;

    while (NULL != (content = container_sl_get_first( &SnapdataCollectors ))){
        _collector_free( (snapdata_collector_t*)content );
        content_sl_free( (content_sl_t*)content );
        content = NULL;
    }

    res = container_sl_done( &SnapdataCollectors );
    if (res != SUCCESS){
        log_err( "Failed to free snapstore collectors container" );
    }
    return res;
}


int _collector_init( snapdata_collector_t* collector, dev_t dev_id, void* MagicUserBuff, size_t MagicLength )
{
    int res = SUCCESS;

    collector->fail_code = SUCCESS;

    collector->dev_id = dev_id;

    res = blk_dev_open( collector->dev_id, &collector->device );
    if (res != SUCCESS){
        log_err_format( "Unable to initialize snapstore collector: failed to open device [%d:%d]. errno=%d", MAJOR( collector->dev_id ), MINOR( collector->dev_id ), res );
        return res;
    }

    collector->magic_size = MagicLength;
    collector->magic_buff = kmalloc( collector->magic_size, GFP_KERNEL | __GFP_RETRY_MAYFAIL );

    if (collector->magic_buff == NULL){
        log_err( "Unable to initialize snapstore collector: not enough memory" );
        return -ENOMEM;
    }
    if (0 != copy_from_user( collector->magic_buff, MagicUserBuff, collector->magic_size )){
        log_err( "Unable to initialize snapstore collector: invalid user buffer" );
        return -ENODATA;
    }
#ifdef SNAPDATA_SPARSE_CHANGES
    sparsebitmap_create( &collector->changes_sparse, 0, blk_dev_get_capacity( collector->device ) );
#else
    {
        stream_size_t bitmap_size = blk_dev_get_capacity( collector->device ) / BITS_PER_BYTE;
        size_t page_count = (size_t)(bitmap_size >> PAGE_SHIFT);
        if ((bitmap_size & (PAGE_SIZE - 1)) != 0)
            ++page_count;

        log_tr_lld( "Create bitmap for snapstore collector. Size ", bitmap_size );

        collector->start_index = 0;
        collector->length = blk_dev_get_capacity( collector->device );

        collector->changes = page_array_alloc( page_count, GFP_KERNEL );
        if (collector->changes == NULL){
            return -ENOMEM;
        }
        page_array_memset( collector->changes, 0 );
    }
#endif

    mutex_init(&collector->locker);

    {
        struct super_block* sb = NULL;
        res = blk_freeze_bdev( collector->dev_id, collector->device, &sb);
        if (res == SUCCESS){
#ifdef CONFIG_BLK_FILTER
            res = tracker_queue_ref(collector->device->bd_disk, collector->device->bd_partno, &collector->tracker_queue);
#else
            res = tracker_queue_ref(bdev_get_queue(collector->device), &collector->tracker_queue);
#endif
            if (res != SUCCESS)
                log_err("Unable to initialize snapstore collector: failed to reference tracker queue");

            sb = blk_thaw_bdev(collector->dev_id, collector->device, sb);
        }
    }

    return res;
}


void _collector_stop( snapdata_collector_t* collector )
{
    if (collector->tracker_queue != NULL){
        tracker_queue_unref( collector->tracker_queue );
        collector->tracker_queue = NULL;
    }
}


void _collector_free( snapdata_collector_t* collector )
{
    _collector_stop( collector );
#ifdef SNAPDATA_SPARSE_CHANGES
    sparsebitmap_destroy( &collector->changes_sparse );
#else
    if (collector->changes != NULL)
        page_array_free( collector->changes );
#endif
    if (collector->magic_buff != NULL){
        kfree( collector->magic_buff );
        collector->magic_buff = NULL;
    }

    if (collector->device != NULL){
        blk_dev_close( collector->device );
        collector->device = NULL;
    }
}


int snapdata_collect_LocationStart( dev_t dev_id, void* MagicUserBuff, size_t MagicLength )
{
    snapdata_collector_t* collector = NULL;
    int res = -ENOTSUPP;

    log_tr_dev_t( "Start collecting snapstore data location on device ", dev_id );

    collector = (snapdata_collector_t*)content_sl_new( &SnapdataCollectors );
    if (NULL == collector){
        log_err( "Unable to start collecting snapstore data location: not enough memory" );
        return  -ENOMEM;
    }

    res = _collector_init( collector, dev_id, MagicUserBuff, MagicLength );
    if (res == SUCCESS){
        container_sl_push_back( &SnapdataCollectors, &collector->content );
    }else{
        _collector_free( collector );

        content_sl_free( &collector->content );
        collector = NULL;
    }

    return res;
}

void rangelist_calculate(rangelist_t* rangelist, sector_t *ranges_length, size_t *count, bool make_output)
{
    //calculate and show information about ranges
    range_t* rg;
    RANGELIST_FOREACH_BEGIN((*rangelist), rg)
    {
        *ranges_length += rg->cnt;
        ++*count;
        if (make_output){
            log_tr_sect("  ofs=", rg->ofs);
            log_tr_sect("  cnt=", rg->cnt);
        }
    }
    RANGELIST_FOREACH_END();
    if (make_output){
        log_tr_sz("range_count=", *count);
        log_tr_sect("ranges_length=", *ranges_length);
    }
}

#ifndef SNAPDATA_SPARSE_CHANGES
int page_array_convert2rangelist(page_array_t* changes, rangelist_t* rangelist, stream_size_t start_index, stream_size_t length)
{
    int res = SUCCESS;
    range_t rg = { 0 };
    size_t index = 0;

    while (index < length){
        bool bit;
        res = page_array_bit_get(changes, index, &bit);
        if (res != SUCCESS)
            break;

        if (bit){
            if (rg.cnt == 0)
                rg.ofs = start_index + index;
            ++rg.cnt;
        }
        else{
            if (rg.cnt == 0){
                // nothing
            }
            else{
                res = rangelist_add(rangelist, &rg);
                rg.cnt = 0;
            }
        }
        ++index;
    }

    if ((res == SUCCESS) && (rg.cnt != 0)){
        res = rangelist_add(rangelist, &rg);
        rg.cnt = 0;
    }        
    return res;
}
#endif

int snapdata_collect_LocationGet( dev_t dev_id, rangelist_t* rangelist, size_t* ranges_count )
{
    size_t count = 0;
    sector_t ranges_length = 0;
    snapdata_collector_t* collector = NULL;
    int res;

    log_tr( "Get snapstore data location");
    res = snapdata_collect_Get( dev_id, &collector );
    if (res != SUCCESS){
        log_err_dev_t( "Unable to get snapstore data location: cannot find collector for device ", dev_id );
        return res;
    }

    _collector_stop( collector );

    if (collector->fail_code != SUCCESS){
        log_err_d( "Unable to get snapstore data location: collecting failed with errno=", 0-collector->fail_code );
        return collector->fail_code;
    }
#ifdef SNAPDATA_SPARSE_CHANGES
    res = sparsebitmap_convert2rangelist(&collector->changes_sparse, rangelist, collector->changes_sparse.start_index);
#else
    res = page_array_convert2rangelist(collector->changes, rangelist, collector->start_index, collector->length);
#endif
    if (res == SUCCESS){
        rangelist_calculate(rangelist, &ranges_length, &count, false);
        log_tr_llx("Collection size: ", collector->collected_size);
        log_tr_llx("Already set size: ", collector->already_set_size);
        log_tr_d("Ranges count: ", count);

        *ranges_count = count;
    }
    return res;
}

int snapdata_collect_LocationComplete( dev_t dev_id )
{
    snapdata_collector_t* collector = NULL;
    int res;

    log_tr( "Collecting snapstore data location completed" );
    res = snapdata_collect_Get( dev_id, &collector );
    if (res != SUCCESS){
        log_err_dev_t( "Unable to complete collecting snapstore data location: cannot find collector for device ", dev_id );
        return res;
    }

    _collector_free( collector );
    container_sl_free( &collector->content );

    return res;
}


int snapdata_collect_Get( dev_t dev_id, snapdata_collector_t** p_collector )
{
    int res = -ENODATA;
    content_sl_t* content = NULL;
    snapdata_collector_t* collector = NULL;
    CONTAINER_SL_FOREACH_BEGIN( SnapdataCollectors, content )
    {
        collector = (snapdata_collector_t*)content;

        if (dev_id == collector->dev_id){
            *p_collector = collector;
            res = SUCCESS;    //don`t continue
        }
    }
    CONTAINER_SL_FOREACH_END( SnapdataCollectors );
    return res;
}

#ifdef CONFIG_BLK_FILTER
int snapdata_collect_Find(struct bio *bio, snapdata_collector_t** p_collector)
{
    int res = -ENODATA;
    content_sl_t* content = NULL;
    snapdata_collector_t* collector = NULL;
    CONTAINER_SL_FOREACH_BEGIN(SnapdataCollectors, content)
    {
        collector = (snapdata_collector_t*)content;
        
        if ((collector->device->bd_disk == bio->bi_disk) && (collector->device->bd_partno == bio->bi_partno)) {
            *p_collector = collector;
            res = SUCCESS;    //don`t continue
        }
    }
    CONTAINER_SL_FOREACH_END(SnapdataCollectors);
    return res;
}
#else //
int snapdata_collect_Find( struct request_queue *q, struct bio *bio, snapdata_collector_t** p_collector )
{
    int res = -ENODATA;
    content_sl_t* content = NULL;
    snapdata_collector_t* collector = NULL;
    CONTAINER_SL_FOREACH_BEGIN( SnapdataCollectors, content )
    {
        collector = (snapdata_collector_t*)content;

        if ( (q == bdev_get_queue( collector->device ))
            && (bio_bi_sector( bio ) >= blk_dev_get_start_sect( collector->device ))
            && ( bio_bi_sector( bio ) < (blk_dev_get_start_sect( collector->device ) + blk_dev_get_capacity( collector->device )))
        ){
            *p_collector = collector;
            res = SUCCESS;    //don`t continue
        }
    }
    CONTAINER_SL_FOREACH_END( SnapdataCollectors );
    return res;
}
#endif //CONFIG_BLK_FILTER

int _snapdata_collect_bvec( snapdata_collector_t* collector, sector_t ofs, struct bio_vec* bvec )
{
    int res = SUCCESS;
    unsigned int bv_len;
    unsigned int bv_offset;
    sector_t buff_ofs;
    void* mem;
    stream_size_t sectors_map = 0;
    bv_len = bvec->bv_len;
    bv_offset = bvec->bv_offset;

    if ((bv_len >> SECTOR_SHIFT) > (sizeof( stream_size_t ) * 8)){ //because sectors_map have only 64 bits.
        log_err_format( "Unable to collect snapstore data location: large PAGE_SIZE [%ld] is not supported yet. bv_len=%d", PAGE_SIZE, bv_len );
        return -EINVAL;
    }

    mem = kmap_atomic( bvec->bv_page ) ;
    for (buff_ofs = bv_offset; buff_ofs < ( bv_offset + bv_len ); buff_ofs+=SECTOR_SIZE){
        size_t compare_len = min( (size_t)SECTOR_SIZE, collector->magic_size );

        if (0 == memcmp( mem + buff_ofs, collector->magic_buff, compare_len ))
            sectors_map |= (stream_size_t)1 << (stream_size_t)(buff_ofs >> SECTOR_SHIFT);
    }
    kunmap_atomic( mem );

    mutex_lock(&collector->locker);
    for (buff_ofs = bv_offset; buff_ofs < (bv_offset + bv_len); buff_ofs += SECTOR_SIZE){
        sector_t buff_ofs_sect = sector_from_size( buff_ofs );
        if ((1ull << buff_ofs_sect) & sectors_map)
        {
            sector_t index = ofs + buff_ofs_sect;
#ifdef SNAPDATA_SPARSE_CHANGES
            res = sparsebitmap_Set(&collector->changes_sparse, index, true);
#else
            res = page_array_bit_set(collector->changes, (index - collector->start_index), true);
#endif
            if (res == SUCCESS){
                collector->collected_size += SECTOR_SIZE;
/*
                if (index == collector->collected_end + 1)
                    collector->collected_end++;
                else {
                    log_tr_sect("Collected index from: ", collector->collected_begin);
                    log_tr_sect("Collected index to  : ", collector->collected_end);

                    collector->collected_begin = index;
                    collector->collected_end = index;
                }
*/
            }else{
                if (res == -EALREADY){
                    collector->already_set_size += SECTOR_SIZE;
/*
                    if (index == collector->already_set_end + 1)
                        collector->already_set_end++;
                    else {
                        log_tr_sect("Already set index from: ", collector->already_set_begin);
                        log_tr_sect("Already set index to  : ", collector->already_set_end);

                        collector->already_set_begin = index;
                        collector->already_set_end = index;
                    }
*/
                }else{
                    log_err_format("Failed to collect snapstore data location. Sector=%lld, errno=%d", index, res);
                    break;
                }
            }
        }
    }
    mutex_unlock(&collector->locker);
    return SUCCESS;
}


void snapdata_collect_Process( snapdata_collector_t* collector, struct bio *bio )
{
    sector_t ofs;
    sector_t size;

    if (unlikely(bio_data_dir( bio ) == READ))//read do not process
        return;

    if (unlikely(collector->fail_code != SUCCESS))
        return;

    ofs = bio_bi_sector( bio ) - blk_dev_get_start_sect( collector->device );
    size = bio_sectors( bio );

    {
        struct bio_vec bvec;
        struct bvec_iter iter;

        bio_for_each_segment( bvec, bio, iter ) {


            int err = _snapdata_collect_bvec( collector, ofs, &bvec );
            ofs += sector_from_size( bvec.bv_len );
            if (err){
                collector->fail_code = err;
                log_err_d( "Failed to collect snapstore data location. errno=", collector->fail_code );
                break;
            }
        }
    }
}

