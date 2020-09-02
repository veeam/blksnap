#pragma once


typedef struct bitmap_sync_s{
    spinlock_t lock;
    unsigned int max_bit_count;
    unsigned long* map;
}bitmap_sync_t;

int bitmap_sync_init( bitmap_sync_t* bitmap, unsigned int bit_count );
void bitmap_sync_done( bitmap_sync_t* pbitmap );

void bitmap_sync_set( bitmap_sync_t* bitmap, unsigned int index );
void bitmap_sync_clear( bitmap_sync_t* bitmap, unsigned int index );

int bitmap_sync_find_clear_and_set( bitmap_sync_t* bitmap );
