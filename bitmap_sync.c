#include "stdafx.h"
#include "bitmap_sync.h"

#define SECTION "bitmap_sync"

int bitmap_sync_init( bitmap_sync_t* bitmap, unsigned int bit_count )
{
    spin_lock_init( &bitmap->lock );

    bitmap->max_bit_count = roundup( bit_count, 8 * sizeof( unsigned long ) );
    bitmap->map = dbg_kzalloc( (size_t)bitmap->max_bit_count >> 3, GFP_KERNEL );
    if (bitmap->map == NULL){
        return -ENOMEM;
    }
    return SUCCESS;
}

void bitmap_sync_done( bitmap_sync_t* bitmap )
{
    if (bitmap->map != NULL){
        dbg_kfree( bitmap->map );
        bitmap->map = NULL;
        bitmap->max_bit_count = 0;
    }
}

void bitmap_sync_set( bitmap_sync_t* bitmap, unsigned int index )
{
    spin_lock( &bitmap->lock );
    do{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,34)
        {
            unsigned int long_inx = index / BITS_PER_LONG;
            unsigned int bit_inx = index % BITS_PER_LONG;

            bitmap->map[long_inx] |= (unsigned long)1 << bit_inx;
        }
#else
        bitmap_set( bitmap->map, index, (int)1 );
#endif
    } while (false);
    spin_unlock( &bitmap->lock );
}

void bitmap_sync_clear( bitmap_sync_t* bitmap, unsigned int index )
{
    spin_lock( &bitmap->lock );
    do{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,34)
        {
            unsigned int long_inx = index / BITS_PER_LONG;
            unsigned int bit_inx = index % BITS_PER_LONG;

            bitmap->map[long_inx] &= ~( (unsigned long)1 << bit_inx );
        }
#else
        bitmap_clear( bitmap->map, index, (int)1 );
#endif
    } while (false);
    spin_unlock( &bitmap->lock );
}

int bitmap_sync_find_clear_and_set( bitmap_sync_t* bitmap )
{
    int index = 0;
    spin_lock( &bitmap->lock );
    do{
        index = bitmap_find_free_region( bitmap->map, bitmap->max_bit_count, 0 );
    } while (false);
    spin_unlock( &bitmap->lock );
    return index;
}
