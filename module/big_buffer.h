#pragma once

struct big_buffer
{
	size_t pg_cnt;
	u8* pg[0];
};

struct big_buffer* big_buffer_alloc( size_t count, int gfp_opt );
void big_buffer_free( struct big_buffer* bbuff );

size_t big_buffer_copy_to_user( char __user* dst_user_buffer, size_t offset, struct big_buffer* bbuff, size_t length );
size_t big_buffer_copy_from_user( const char __user* src_user_buffer, size_t offset, struct big_buffer* bbuff, size_t length );

void* big_buffer_get_element( struct big_buffer* bbuff, size_t index, size_t sizeof_element );

void big_buffer_memset( struct big_buffer* bbuff, int value );
void big_buffer_memcpy( struct big_buffer *dst, struct big_buffer *src );

//byte access
int big_buffer_byte_get( struct big_buffer *bbuff, size_t inx, u8* value );
int big_buffer_byte_set( struct big_buffer *bbuff, size_t inx, u8 value );
