#pragma once
#ifdef PERSISTENT_CBT

#define CBT_STORAGE_MAGIC "vcbtdata"

#pragma pack(push,1)
typedef struct cbt_storage_page_s
{
    char magic[8];
    uint32_t crc;
    uint32_t number;
    uint64_t tv_sec;    // seconds
    uint32_t tv_nsec;    // nanoseconds
    uint32_t padding0;
    unsigned char data[0];
} cbt_storage_page_t;
#pragma pack(pop)

#define CBT_PAGE_DATA_SIZE (PAGE_SIZE - offsetof(cbt_storage_page_t, data))

#pragma pack(push,8)
typedef struct cbt_storage_accessor_s
{
    struct block_device* device;
    rangevector_t* rangevector;

    struct page* pg;
    cbt_storage_page_t* page;

    unsigned long long page_count;
    unsigned long long page_number;
    unsigned long long used_page_count;
    size_t page_offset;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,20,0)
    struct timespec time;//cbt data time marker
#else
    struct timespec64 time;//cbt data time marker
#endif
    unsigned long long padding;
}cbt_storage_accessor_t;
#pragma pack(pop)

int cbt_storage_open(cbt_persistent_parameters_t* params, cbt_storage_accessor_t* accessor);
void cbt_storage_close(cbt_storage_accessor_t* accessor);

int cbt_storage_check(cbt_storage_accessor_t* accessor);

int cbt_storage_prepare4read(cbt_storage_accessor_t* accessor);
int cbt_storage_prepare4write(cbt_storage_accessor_t* accessor);

int cbt_storage_read(cbt_storage_accessor_t* accessor, void* dst, size_t sz);
int cbt_storage_write(cbt_storage_accessor_t* accessor, void* src, size_t sz);
int cbt_storage_write_finish(cbt_storage_accessor_t* accessor);

#endif//PERSISTENT_CBT
