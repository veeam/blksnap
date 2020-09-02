#include "stdafx.h"
#include "rangelist.h"

#define SECTION "ranges    "

void rangelist_init( rangelist_t* rglist )
{
    INIT_LIST_HEAD( &rglist->head );
}

static inline rangelist_el_t* _rangelist_get_first( rangelist_t* rglist )
{
    rangelist_el_t* el = NULL;
    if (!list_empty( &rglist->head )){
        el = list_entry( rglist->head.next, rangelist_el_t, link );
        list_del( &el->link );
    }
    return el;
}

void rangelist_done( rangelist_t* rglist )
{
    rangelist_el_t* el;
    while (NULL != (el = _rangelist_get_first( rglist )))
        dbg_kfree( el );
}

int rangelist_add( rangelist_t* rglist, range_t* rg )
{
    rangelist_el_t* el = dbg_kzalloc( sizeof( rangelist_el_t ), GFP_KERNEL );
    if (el == NULL)
        return -ENOMEM;

    INIT_LIST_HEAD( &el->link );

    el->rg.ofs = rg->ofs;
    el->rg.cnt = rg->cnt;

    list_add_tail( &el->link, &rglist->head );

    return SUCCESS;
}

int rangelist_get( rangelist_t* rglist, range_t* rg )
{
    rangelist_el_t* el = _rangelist_get_first( rglist );
    if (el == NULL)
        return -ENODATA;

    rg->ofs = el->rg.ofs;
    rg->cnt = el->rg.cnt;

    dbg_kfree( el );

    return SUCCESS;
}

bool rangelist_empty( rangelist_t* rglist )
{
    return list_empty( &rglist->head );
}
