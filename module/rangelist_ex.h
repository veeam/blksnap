#pragma once

#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV

#include "range.h"

//  IS NOT LOCKED

typedef struct rangelist_ex_s
{
    struct list_head head;
}rangelist_ex_t;

typedef struct range_el_ex_s
{
    struct list_head link;
    range_t rg;

    void* extension;

}rangelist_el_ex_t;


void rangelist_ex_init( rangelist_ex_t* rglist );
void rangelist_ex_done( rangelist_ex_t* rglist );

int rangelist_ex_add( rangelist_ex_t* rglist, range_t* rg, void* extension );
int rangelist_ex_get( rangelist_ex_t* rglist, range_t* rg, void** p_extension );

bool rangelist_ex_empty( rangelist_ex_t* rglist );

static inline void rangelist_ex_copy( rangelist_ex_t* dst, rangelist_ex_t* src )
{
    struct list_head* next = src->head.next;
    struct list_head* prev = src->head.prev;

    dst->head.next = next;
    dst->head.prev = prev;

    next->prev = &dst->head;
    prev->next = &dst->head;
}

#define RANGELIST_EX_FOREACH_BEGIN( rglist, rg, ex ) \
if (!list_empty( &rglist.head )){ \
    struct list_head* _list_head; \
    list_for_each( _list_head, &rglist.head ){ \
        rangelist_el_ex_t* _el = list_entry( _list_head, rangelist_el_ex_t, link ); \
        rg = &_el->rg; \
        ex = &_el->extension;


#define RANGELIST_EX_FOREACH_END( ) \
    } \
}

#endif //CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
