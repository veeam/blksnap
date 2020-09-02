#pragma once

#include "sector.h"

typedef struct page_info_s{
    struct page* page;
    void* addr;
}page_info_t;

typedef struct page_array_s
{
    size_t pg_cnt;
    page_info_t pg[0];
}page_array_t;

void page_arrays_init( void );
void page_arrays_print_state( void );

page_array_t* page_array_alloc( size_t count, int gfp_opt );
void page_array_free( page_array_t* arr );

size_t page_array_pages2mem( void* dst_buffer, size_t arr_ofs, page_array_t* arr, size_t length );
size_t page_array_mem2pages( void* src_buffer, size_t arr_ofs, page_array_t* arr, size_t length );

size_t page_array_page2user( char __user* dst_user_buffer, size_t arr_ofs, page_array_t* arr, size_t length );
size_t page_array_user2page( const char __user* src_user_buffer, size_t arr_ofs, page_array_t* arr, size_t length );

size_t page_count_calc( size_t buffer_size );
size_t page_count_calc_sectors( sector_t range_start_sect, sector_t range_cnt_sect );

void* page_get_element( page_array_t* arr, size_t index, size_t sizeof_element );
char* page_get_sector( page_array_t* arr, sector_t arr_ofs );

//
void page_array_memset( page_array_t* arr, int value );
void page_array_memcpy( page_array_t* dst, page_array_t* src );

//pointer access
int page_array_ptr_get(page_array_t* arr, size_t inx, void** value);
int page_array_ptr_set(page_array_t* arr, size_t inx, void* value);

//byte access
int page_array_byte_get( page_array_t* arr, size_t inx, byte_t* value );
int page_array_byte_set( page_array_t* arr, size_t inx, byte_t value );

// bit access
int page_array_bit_get( page_array_t* arr, size_t inx, bool* value );
int page_array_bit_set( page_array_t* arr, size_t inx, bool value );
