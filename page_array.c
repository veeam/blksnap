#include "stdafx.h"
#include "page_array.h"

#define SECTION "page_array"
//#include "log_format.h"

atomic64_t page_alloc_count;
atomic64_t page_free_count;
atomic64_t page_array_alloc_count;
atomic64_t page_array_free_count;

void page_arrays_init( void )
{
    atomic64_set( &page_alloc_count, 0 );
    atomic64_set( &page_free_count, 0 );

    atomic64_set( &page_array_alloc_count, 0 );
    atomic64_set( &page_array_free_count, 0 );
}

void page_arrays_print_state( void )
{
    log_tr( "" );
    log_tr( "Page array state:" );
    log_tr_lld( "pages allocated: ", (long long int)atomic64_read( &page_alloc_count ) );
    log_tr_lld( "pages freed: ", (long long int)atomic64_read( &page_free_count ) );
    log_tr_lld( "pages in use: ", (long long int)atomic64_read( &page_alloc_count ) - (long long int)atomic64_read( &page_free_count ) );

    log_tr_lld( "arrays allocated: ", (long long int)atomic64_read( &page_array_alloc_count ) );
    log_tr_lld( "arrays freed: ", (long long int)atomic64_read( &page_array_free_count ) );
    log_tr_lld( "arrays in use: ", (long long int)atomic64_read( &page_array_alloc_count ) - (long long int)atomic64_read( &page_array_free_count ) );
}

page_array_t* page_array_alloc( size_t count, int gfp_opt )
{
    int res = SUCCESS;
    size_t inx;
    page_array_t* arr = NULL;
    while (NULL == (arr = dbg_kzalloc( sizeof( page_array_t ) + count*sizeof( page_info_t ), gfp_opt ))){
        log_err( "Failed to allocate page_array buffer" );
        return NULL;
    }
    arr->pg_cnt = count;
    for (inx = 0; inx < arr->pg_cnt; ++inx){
        if (NULL == (arr->pg[inx].page = alloc_page( gfp_opt ))){
            log_err( "Failed to allocate page" );
            res = -ENOMEM;
            break;
        }
        arr->pg[inx].addr = page_address( arr->pg[inx].page );
        atomic64_inc( &page_alloc_count );
    }
    atomic64_inc( &page_array_alloc_count );

    if (SUCCESS == res)
        return arr;
    
    page_array_free( arr );
    return NULL;
}

void page_array_free( page_array_t* arr )
{
    size_t inx;
    size_t count = arr->pg_cnt;
    if (arr == NULL)
        return;

    for (inx = 0; inx < count; ++inx){
        if (arr->pg[inx].page != NULL){
            free_page( (unsigned long)(arr->pg[inx].addr) );
            atomic64_inc( &page_free_count );
        }
    }
    dbg_kfree( arr );
    atomic64_inc( &page_array_free_count );
}

size_t page_array_pages2mem( void* dst, size_t arr_ofs, page_array_t* arr, size_t length )
{
    int page_inx = arr_ofs / PAGE_SIZE;
    size_t processed_len = 0;
    void* src;
    {//first
        size_t unordered = arr_ofs & (PAGE_SIZE - 1);
        size_t page_len = min_t( size_t, ( PAGE_SIZE - unordered ), length );

        src = arr->pg[page_inx].addr;
        memcpy( dst + processed_len, src + unordered, page_len );

        ++page_inx;
        processed_len += page_len;
    }
    while ((processed_len < length) && (page_inx < arr->pg_cnt))
    {
        size_t page_len = min_t( size_t, PAGE_SIZE, (length - processed_len) );

        src = arr->pg[page_inx].addr;
        memcpy( dst + processed_len, src, page_len );

        ++page_inx;
        processed_len += page_len;
    }

    return processed_len;
}

size_t page_array_mem2pages( void* src, size_t arr_ofs, page_array_t* arr, size_t length )
{
    int page_inx = arr_ofs / PAGE_SIZE;
    size_t processed_len = 0;
    void* dst;
    {//first
        size_t unordered = arr_ofs & (PAGE_SIZE - 1);
        size_t page_len = min_t( size_t, (PAGE_SIZE - unordered), length );

        dst = arr->pg[page_inx].addr;
        memcpy( dst + unordered, src + processed_len, page_len );

        ++page_inx;
        processed_len += page_len;
    }
    while ((processed_len < length) && (page_inx < arr->pg_cnt))
    {
        size_t page_len = min_t( size_t, PAGE_SIZE, (length - processed_len) );

        dst = arr->pg[page_inx].addr;
        memcpy( dst, src + processed_len, page_len );

        ++page_inx;
        processed_len += page_len;
    }

    return processed_len;
}

size_t page_array_page2user( char __user* dst_user, size_t arr_ofs, page_array_t* arr, size_t length )
{
    size_t left_data_length;
    int page_inx = arr_ofs / PAGE_SIZE;
    size_t processed_len = 0;

    size_t unordered = arr_ofs & (PAGE_SIZE - 1);
    if (unordered != 0)//first
    {
        size_t page_len = min_t( size_t, (PAGE_SIZE - unordered), length );

        left_data_length = copy_to_user( dst_user + processed_len, arr->pg[page_inx].addr  + unordered, page_len );
        if (0 != left_data_length){
            log_err( "Failed to copy data from page array to user buffer" );
            return processed_len;
        }

        ++page_inx;
        processed_len += page_len;
    }
    while ((processed_len < length) && (page_inx < arr->pg_cnt))
    {
        size_t page_len = min_t( size_t, PAGE_SIZE, (length - processed_len) );

        left_data_length = copy_to_user( dst_user + processed_len, arr->pg[page_inx].addr, page_len );
        if (0 != left_data_length){
            log_err( "Failed to copy data from page array to user buffer" );
            break;
        }

        ++page_inx;
        processed_len += page_len;
    }

    return processed_len;
}

size_t page_array_user2page( const char __user* src_user, size_t arr_ofs, page_array_t* arr, size_t length )
{
    size_t left_data_length;
    int page_inx = arr_ofs / PAGE_SIZE;
    size_t processed_len = 0;

    size_t unordered = arr_ofs & (PAGE_SIZE - 1);
    if (unordered != 0)//first
    {
        size_t page_len = min_t( size_t, (PAGE_SIZE - unordered), length );

        left_data_length = copy_from_user( arr->pg[page_inx].addr + unordered, src_user + processed_len, page_len );
        if (0 != left_data_length){
            log_err( "Failed to copy data from page array to user buffer" );
            return processed_len;
        }

        ++page_inx;
        processed_len += page_len;
    }
    while ((processed_len < length) && (page_inx < arr->pg_cnt))
    {
        size_t page_len = min_t( size_t, PAGE_SIZE, (length - processed_len) );

        left_data_length = copy_from_user( arr->pg[page_inx].addr, src_user + processed_len, page_len );
        if (0 != left_data_length){
            log_err( "Failed to copy data from page array to user buffer" );
            break;
        }

        ++page_inx;
        processed_len += page_len;
    }

    return processed_len;
}

size_t page_count_calc( size_t buffer_size )
{
    size_t page_count = buffer_size / PAGE_SIZE;

    if ( buffer_size & (PAGE_SIZE - 1) )
        page_count += 1;
    return page_count;
}

size_t page_count_calc_sectors( sector_t range_start_sect, sector_t range_cnt_sect )
{
    size_t page_count = range_cnt_sect / SECTORS_IN_PAGE;

    if (unlikely( range_cnt_sect & (SECTORS_IN_PAGE - 1) ))
        page_count += 1;
    return page_count;
}

void* page_get_element( page_array_t* arr, size_t index, size_t sizeof_element )
{
    size_t elements_in_page = PAGE_SIZE / sizeof_element;
    size_t pg_inx = index / elements_in_page;
    size_t pg_ofs = (index - (pg_inx * elements_in_page)) * sizeof_element;

    return (arr->pg[pg_inx].addr + pg_ofs);
}

char* page_get_sector( page_array_t* arr, sector_t arr_ofs )
{
    size_t pg_inx = arr_ofs >> (PAGE_SHIFT - SECTOR_SHIFT);
    size_t pg_ofs = sector_to_size( arr_ofs & ((1 << (PAGE_SHIFT - SECTOR_SHIFT)) - 1) );

    return (arr->pg[pg_inx].addr + pg_ofs);
}

void page_array_memset( page_array_t* arr, int value )
{
    size_t inx;
    for (inx = 0; inx < arr->pg_cnt; ++inx){
        void* ptr = arr->pg[inx].addr;
        memset( ptr, value, PAGE_SIZE );
    }
}

void page_array_memcpy( page_array_t* dst, page_array_t* src )
{
    size_t inx;
    size_t count = min_t( size_t, dst->pg_cnt, src->pg_cnt );

    for (inx = 0; inx < count; ++inx){
        void* dst_ptr = dst->pg[inx].addr ;
        void* src_ptr = src->pg[inx].addr;
        memcpy( dst_ptr, src_ptr, PAGE_SIZE );
    }
}

#define _PAGE_INX_CHECK(arr, inx, page_inx) \
if (page_inx >= arr->pg_cnt){ \
    log_err_sz( "Invalid index ", inx ); \
    log_err_sz( "page_inx=", page_inx ); \
    log_err_sz( "page_cnt=", arr->pg_cnt ); \
    return -ENODATA; \
}

#define POINTERS_IN_PAGE (PAGE_SIZE/sizeof(void*))

int page_array_ptr_get(page_array_t* arr, size_t inx, void** value)
{

    size_t page_inx = inx / POINTERS_IN_PAGE;
    _PAGE_INX_CHECK(arr, inx, page_inx);

    {
        size_t pos = inx & (POINTERS_IN_PAGE - 1);
        void** ptr = arr->pg[page_inx].addr;
        *value = ptr[pos];
    }
    return SUCCESS;
}

int page_array_ptr_set(page_array_t* arr, size_t inx, void* value)
{
    size_t page_inx = inx / POINTERS_IN_PAGE;
    _PAGE_INX_CHECK(arr, inx, page_inx);

    {
        size_t byte_pos = inx & (POINTERS_IN_PAGE - 1);
        void** ptr = arr->pg[page_inx].addr;
        ptr[byte_pos] = value;
    }
    return SUCCESS;
}

int page_array_byte_get( page_array_t* arr, size_t inx, byte_t* value )
{
    size_t page_inx = inx >> PAGE_SHIFT;
    _PAGE_INX_CHECK( arr, inx, page_inx );

    {
        size_t byte_pos = inx & (PAGE_SIZE - 1);
        byte_t* ptr = arr->pg[page_inx].addr;
        *value = ptr[byte_pos];
    }
    return SUCCESS;
}

int page_array_byte_set( page_array_t* arr, size_t inx, byte_t value )
{
    size_t page_inx = inx >> PAGE_SHIFT;
    _PAGE_INX_CHECK( arr, inx, page_inx );

    {
        size_t byte_pos = inx & (PAGE_SIZE - 1);
        byte_t* ptr = arr->pg[page_inx].addr;
        ptr[byte_pos] = value;
    }
    return SUCCESS;
}

int page_array_bit_get( page_array_t* arr, size_t inx, bool* value )
{
    byte_t v;
    size_t byte_inx = (inx / BITS_PER_BYTE);
    int res = page_array_byte_get( arr, byte_inx, &v );
    if (SUCCESS != res)
        return res;

    {
        size_t bit_inx = (inx & (BITS_PER_BYTE - 1));
        *value = v & (1 << bit_inx);
    }
    return SUCCESS;
}

int page_array_bit_set( page_array_t* arr, size_t inx, bool value )
{
    size_t byte_inx = (inx / BITS_PER_BYTE);
    size_t page_inx = byte_inx >> PAGE_SHIFT;
    _PAGE_INX_CHECK( arr, inx, page_inx );

    {
        byte_t v;
        size_t bit_inx = (inx & (BITS_PER_BYTE - 1));

        size_t byte_pos = byte_inx & (PAGE_SIZE - 1);
        byte_t* ptr = arr->pg[page_inx].addr;

        v = ptr[byte_pos];
        if (value){
            if (unlikely(v & (1 << bit_inx)))
                return -EALREADY;
            v |= (1 << bit_inx);
        }
        else{
            if (unlikely(0 == (v & (1 << bit_inx))))
                return -EALREADY;
            v &= ~(1 << bit_inx);
        }

        ptr[byte_pos] = v;
    }
    return SUCCESS;
}
