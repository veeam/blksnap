#include "stdafx.h"
#ifdef SNAPSTORE_MULTIDEV

#include "rangelist_ex.h"

#define SECTION "ranges_ex "

void rangelist_ex_init( rangelist_ex_t* rglist )
{
    INIT_LIST_HEAD( &rglist->head );
}

static inline rangelist_el_ex_t* _rangelist_ex_get_first( rangelist_ex_t* rglist )
{
    rangelist_el_ex_t* el = NULL;
    if (!list_empty( &rglist->head )){
        el = list_entry( rglist->head.next, rangelist_el_ex_t, link );
        list_del( &el->link );
    }
    return el;
}

void rangelist_ex_done( rangelist_ex_t* rglist )
{
    rangelist_el_ex_t* el;
    while (NULL != (el = _rangelist_ex_get_first( rglist )))
        kfree( el );
}

int rangelist_ex_add( rangelist_ex_t* rglist, range_t* rg, void* extension )
{
    rangelist_el_ex_t* el = kzalloc( sizeof( rangelist_el_ex_t ), GFP_KERNEL );
    if (el == NULL)
        return -ENOMEM;

    INIT_LIST_HEAD( &el->link );

    el->rg.ofs = rg->ofs;
    el->rg.cnt = rg->cnt;
    el->extension = extension;

    list_add_tail( &el->link, &rglist->head );

    return SUCCESS;
}

int rangelist_ex_get( rangelist_ex_t* rglist, range_t* rg, void** p_extension )
{
    rangelist_el_ex_t* el = _rangelist_ex_get_first( rglist );
    if (el == NULL)
        return -ENODATA;

    rg->ofs = el->rg.ofs;
    rg->cnt = el->rg.cnt;
    *p_extension = el->extension;

    kfree( el );

    return SUCCESS;
}

bool rangelist_ex_empty( rangelist_ex_t* rglist )
{
    return list_empty( &rglist->head );
}
#endif //SNAPSTORE_MULTIDEV
