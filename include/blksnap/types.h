#ifndef BLK_SNAP_TYPES_H
#define BLK_SNAP_TYPES_H

#pragma pack(push,1)

struct ioctl_dev_id_s{
    int major;
    int minor;
};

struct cbt_info_s{
    struct ioctl_dev_id_s dev_id;
    unsigned long long dev_capacity;
    unsigned int cbt_map_size;
    unsigned char snap_number;
    unsigned char generationId[16];
};

#pragma pack(pop)

#endif //BLK_SNAP_TYPES_H
