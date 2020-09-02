#pragma once

#if LINUX_VERSION_CODE < KERNEL_VERSION( 4, 13, 0 )
//#ifndef _LINUX_UUID_H_//because sles 15 have kernel 4.12.14, but have implementation like in 4.13

#ifndef UUID_SIZE
#define UUID_SIZE 16
#endif

typedef struct uuid_s
{
    __u8 b[UUID_SIZE];
}veeam_uuid_t;

static inline void veeam_uuid_copy( veeam_uuid_t* dst, veeam_uuid_t* src )
{
    memcpy( dst->b, src->b, UUID_SIZE );
};

static inline bool veeam_uuid_equal( veeam_uuid_t* first, veeam_uuid_t* second )
{
    return (0 == memcmp( first->b, second->b, UUID_SIZE ));
};

static inline void veeam_generate_random_uuid( unsigned char uuid[UUID_SIZE] )
{
    get_random_bytes( uuid, UUID_SIZE );
    /* Set UUID version to 4 --- truly random generation */
    uuid[6] = (uuid[6] & 0x0F) | 0x40;
    /* Set the UUID variant to DCE */
    uuid[8] = (uuid[8] & 0x3F) | 0x80;
};

#else

#include <linux/uuid.h>

#define veeam_uuid_t uuid_t
#define veeam_uuid_copy uuid_copy
#define veeam_uuid_equal uuid_equal
#define veeam_generate_random_uuid generate_random_uuid


#endif 


