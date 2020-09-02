#pragma once

#include "range.h"

//  IS NOT LOCKED

typedef struct rangelist_s
{
    struct list_head head;
}rangelist_t;


typedef struct range_el_s
{
    struct list_head link;
    range_t rg;
}rangelist_el_t;


void rangelist_init( rangelist_t* rglist );

void rangelist_done( rangelist_t* rglist );

int rangelist_add( rangelist_t* rglist, range_t* rg );

int rangelist_get( rangelist_t* rglist, range_t* rg );

bool rangelist_empty( rangelist_t* rglist );

static inline void rangelist_copy( rangelist_t* dst, rangelist_t* src )
{
    struct list_head* next = src->head.next;
    struct list_head* prev = src->head.prev;

    dst->head.next = next;
    dst->head.prev = prev;

    next->prev = &dst->head;
    prev->next = &dst->head;
}



#define RANGELIST_FOREACH_BEGIN( rglist, rg ) \
if (!list_empty( &rglist.head )){ \
    struct list_head* _list_head; \
    list_for_each( _list_head, &rglist.head ){ \
        rangelist_el_t* _el = list_entry( _list_head, rangelist_el_t, link ); \
        rg = &_el->rg;



#define RANGELIST_FOREACH_END( ) \
    } \
}


