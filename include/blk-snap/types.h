#ifndef BLK_SNAP_TYPES_H
#define BLK_SNAP_TYPES_H

#ifdef  __cplusplus
extern "C" {
#endif

//@todo: [TBD] should be a common part or in <linux/*>
#include "../../include/blk-snap/blk-snap-ctl.h"

#pragma pack(push,1)
/*
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
*/

#pragma pack(pop)

struct snap_ranges_space
{
    unsigned int count;
    struct ioctl_range_s ranges[0];
};

#ifdef  __cplusplus
}
#endif

#endif //BLK_SNAP_TYPES_H
