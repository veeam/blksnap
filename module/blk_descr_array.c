#include "stdafx.h"
#include "page_array.h"
#include "blk_descr_array.h"

#define SECTION "blk_descr "
#include "log_format.h"

int blk_descr_array_init( blk_descr_array_t* header, blk_descr_array_index_t first, blk_descr_array_index_t last )
{
    size_t page_count = 0;
    init_rwsem( &header->rw_lock );

    header->first = first;
    header->last = last;

    header->group_count = (size_t)((last + 1 - first) >> BLK_DESCR_GROUP_LENGTH_SHIFT);
    if ((last + 1 - first) & BLK_DESCR_GROUP_LENGTH_MASK)
        ++(header->group_count);

    page_count = page_count_calc(header->group_count * sizeof(blk_descr_array_group_t*));
    header->groups = page_array_alloc( page_count, GFP_KERNEL);
    if (NULL == header->groups){
        blk_descr_array_done( header );
        return -ENOMEM;
    }
    page_array_memset(header->groups, 0);

    log_tr_format("Block description array takes up %lu pages", page_count);

    return SUCCESS;
}

void blk_descr_array_done( blk_descr_array_t* header )
{
    if (header->groups != NULL){

        blk_descr_array_reset( header );

        page_array_free(header->groups);

        header->groups = NULL;
    }
}

void blk_descr_array_reset( blk_descr_array_t* header )
{
    size_t gr_idx;
    if (header->groups != NULL){
        for (gr_idx = 0; gr_idx < header->group_count; ++gr_idx){
            void* group = NULL;
            if (SUCCESS == page_array_ptr_get(header->groups, gr_idx, &group)){
                if (group != NULL){
                    dbg_kfree(group);
                    page_array_ptr_set(header->groups, gr_idx, NULL);
                }
            }
        }
    }
}

int blk_descr_array_set( blk_descr_array_t* header, blk_descr_array_index_t inx, blk_descr_array_el_t value )
{
    int res = SUCCESS;

    down_write( &header->rw_lock );
    do{
        size_t gr_idx;
        size_t val_idx;
        blk_descr_array_group_t* group = NULL;
        unsigned char bits;

        if (!((header->first <= inx) && (inx <= header->last))){
            res = -EINVAL;
            break;
        }

        gr_idx = (size_t)((inx - header->first) >> BLK_DESCR_GROUP_LENGTH_SHIFT);
        if (SUCCESS != page_array_ptr_get(header->groups, gr_idx, (void**)&group)){
            res = -EINVAL;
            break;
        }
        if (group == NULL){

            group = dbg_kzalloc(sizeof(blk_descr_array_group_t), GFP_NOIO);
            if (group == NULL){
                res = -ENOMEM;
                break;
            }
            if (SUCCESS != page_array_ptr_set(header->groups, gr_idx, group)){
                res = -EINVAL;
                break;
            }
        }
        val_idx = (size_t)((inx - header->first) & BLK_DESCR_GROUP_LENGTH_MASK);

        bits = (1 << (val_idx & 0x7));
        if (group->bitmap[val_idx >> 3] & bits){
            // rewrite
        }
        else{
            group->bitmap[val_idx >> 3] |= bits;
            ++group->cnt;
        }
        group->values[val_idx] = value;


    } while (false);
    up_write( &header->rw_lock );

    return res;
}

int blk_descr_array_get( blk_descr_array_t* header, blk_descr_array_index_t inx, blk_descr_array_el_t* p_value )
{
    int res = SUCCESS;

    down_read(&header->rw_lock);
    do{
        size_t gr_idx;
        size_t val_idx;
        blk_descr_array_group_t* group = NULL;
        unsigned char bits;

        if ((inx < header->first) || (header->last < inx)){
            res = -EINVAL;
            break;
        }

        gr_idx = (size_t)((inx - header->first) >> BLK_DESCR_GROUP_LENGTH_SHIFT);

        if (SUCCESS != page_array_ptr_get(header->groups, gr_idx, (void**)&group)){
            res = -EINVAL;
            break;
        }
        if (group == NULL){
            res = -ENODATA;
            break;
        }

        val_idx = (size_t)((inx - header->first) & BLK_DESCR_GROUP_LENGTH_MASK);
        bits = (1 << (val_idx & 0x7));
        if (group->bitmap[val_idx >> 3] & bits)
            *p_value = group->values[val_idx];
        else{
            res = -ENODATA;
            break;
        }
    } while (false);
    up_read(&header->rw_lock);

    return res;
}

