#include "stdafx.h"
#include "rangevector.h"
#include "container_spinlocking.h"

#define SECTION "ranges    "

void rangevector_init( rangevector_t* rangevector, bool use_lock )
{
    rangevector->use_lock = use_lock;

    if (rangevector->use_lock)
        init_rwsem( &rangevector->lock );

    INIT_LIST_HEAD( &rangevector->ranges_head );

    atomic_set( &rangevector->blocks_cnt, 0);
}

void rangevector_done( rangevector_t* rangevector )
{
    rangevector_cleanup( rangevector );
}

void rangevector_cleanup( rangevector_t* rangevector )
{
    while (!list_empty( &rangevector->ranges_head )){
        rangevector_el_t* pCnt = list_entry( rangevector->ranges_head.next, rangevector_el_t, link );

        list_del( &pCnt->link );
        atomic_dec( &rangevector->blocks_cnt );

        dbg_kfree( pCnt );
    }
}

int rangevector_add( rangevector_t* rangevector, range_t* rg )
{
    int res = SUCCESS;

    RANGEVECTOR_WRITE_LOCK( rangevector );
    do{
        rangevector_el_t* el = NULL;

        if (!list_empty( &rangevector->ranges_head )){
            el = list_entry( rangevector->ranges_head.prev, rangevector_el_t, link );
            if ((atomic_read( &el->cnt ) == RANGEVECTOR_EL_CAPACITY))
                el = NULL;
        }

        if (el == NULL){
            el = dbg_kmalloc( sizeof( rangevector_el_t ), GFP_KERNEL );
            if (NULL == el){
                res = -ENOMEM;
                break;
            }
            atomic_set( &el->cnt, 0 );

            INIT_LIST_HEAD( &el->link );
            list_add_tail( &el->link, &rangevector->ranges_head );
            atomic_inc( &rangevector->blocks_cnt );
        }
        {
            int last_index = atomic_read( &el->cnt );

            el->ranges[last_index].ofs = rg->ofs;
            el->ranges[last_index].cnt = rg->cnt;

            atomic_inc( &el->cnt );
        }
    } while (false);
    RANGEVECTOR_WRITE_UNLOCK( rangevector );

    return res;
}

int rangevector_v2p( rangevector_t* rangevector, sector_t virt_offset, sector_t virt_length, sector_t* p_phys_offset, sector_t* p_phys_length )
{
    int result = -ENODATA;
    sector_t virt_left = 0;
    sector_t virt_right = 0;
    rangevector_el_t* el;

    RANGEVECTOR_READ_LOCK( rangevector );
    RANGEVECTOR_FOREACH_EL_BEGIN( rangevector, el )
    {
        size_t inx = 0;
        size_t limit = (size_t)atomic_read( &el->cnt );

        for (inx = 0; inx < limit; ++inx){
            range_t* range = &el->ranges[inx];

            virt_right = virt_left + range->cnt;
            if ((virt_offset >= virt_left) && (virt_offset < virt_right)){
                *p_phys_offset = range->ofs + (virt_offset - virt_left);
                *p_phys_length = min( virt_length, virt_right - virt_offset );

                result = SUCCESS;
                break;
            }
            virt_left = virt_right;
        }
    }
    RANGEVECTOR_FOREACH_EL_END( );
    RANGEVECTOR_READ_UNLOCK( rangevector )
    return result;
}

int rangevector_at( rangevector_t* rangevector, size_t inx, range_t* range )
{
    int result = -ENODATA;
    size_t curr_inx = 0;
    rangevector_el_t* el;
    RANGEVECTOR_READ_LOCK( rangevector );
    RANGEVECTOR_FOREACH_EL_BEGIN( rangevector, el )
    {
        size_t el_cnt = atomic_read( &el->cnt );

        if ((curr_inx <= inx) &&  (inx < (curr_inx + el_cnt))){
            range->ofs = el->ranges[inx - curr_inx].ofs;
            range->cnt = el->ranges[inx - curr_inx].cnt;
            result = SUCCESS;
            break;
        }
        curr_inx += el_cnt;
    }
    RANGEVECTOR_FOREACH_EL_END( );
    RANGEVECTOR_READ_UNLOCK( rangevector )
    return result;
}

void rangevector_sort( rangevector_t* rangevector )
{
    //primitive pair swap sort
    bool changed;
    size_t swap_count = 0;
    size_t ranges_count = rangevector_cnt( rangevector );

    RANGEVECTOR_WRITE_LOCK( rangevector );
    do{
        range_t* prange = NULL;
        range_t* prange_prev = NULL;

        changed = false;
        RANGEVECTOR_FOREACH_BEGIN( rangevector, prange )
        {
            if (prange_prev != NULL){
                if (prange_prev->ofs > prange->ofs){
                    range_t tmp;

                    tmp.ofs = prange_prev->ofs;
                    tmp.cnt = prange_prev->cnt;

                    prange_prev->ofs = prange->ofs;
                    prange_prev->cnt = prange->cnt;

                    prange->ofs = tmp.ofs;
                    prange->cnt = tmp.cnt;
                    changed = true;

                    ++swap_count;
                }
            }
            prange_prev = prange;
        }
        RANGEVECTOR_FOREACH_END( );
    } while (changed);
    RANGEVECTOR_WRITE_UNLOCK( rangevector );

    log_tr_sz( "Sort zero ranges count=", ranges_count );
    log_tr_sz( "Swap count=", swap_count );
}

sector_t rangevector_length( rangevector_t* rangevector )
{
    range_t* prange = NULL;
    sector_t length_sect = 0;
    RANGEVECTOR_READ_LOCK( rangevector );
    RANGEVECTOR_FOREACH_BEGIN( rangevector, prange )
    {
        length_sect += prange->cnt;
    }
    RANGEVECTOR_FOREACH_END( );
    RANGEVECTOR_READ_UNLOCK( rangevector );
    return length_sect;
}

size_t rangevector_cnt( rangevector_t* rangevector )
{
    size_t cnt = 0;
    rangevector_el_t* el;

    RANGEVECTOR_READ_LOCK( rangevector );
    RANGEVECTOR_FOREACH_EL_BEGIN( rangevector, el )
    {
        cnt += (size_t)atomic_read( &el->cnt );
    }
    RANGEVECTOR_FOREACH_EL_END( );
    RANGEVECTOR_READ_UNLOCK( rangevector );
    return cnt;
}

range_t* rangevector_el_find_first_hit( rangevector_el_t* el, sector_t from_sect, sector_t to_sect )
{
    range_t* found = NULL;
    size_t index = 0;
    size_t limit_min = 0;
    size_t limit_max = atomic_read( &el->cnt );

    index = (limit_max - limit_min) / 2;

    do{
        range_t* rg = &el->ranges[index];

        if ((rg->ofs + rg->cnt) <= from_sect){
            size_t increment = (limit_max - index) / 2;
            if (increment == 0)
                break;

            limit_min = index;
            index += increment;
            continue;
        }

        if (rg->ofs >= to_sect){
            size_t decrement = (index - limit_min) / 2;
            if (decrement == 0)
                break;

            limit_max = index;
            index -= decrement;
            continue;
        }

        found = rg; //some one found
        break;
    } while (true);

    //check previous ranges
    if (found != NULL){
        while (index > 0){
            range_t* rg;
            --index;
            rg = &el->ranges[index];

            if ((rg->ofs + rg->cnt) <= from_sect)
                break;

            if (rg->ofs >= to_sect)
                break;

            found = rg; //better found
        }
    }

    return found;
}

