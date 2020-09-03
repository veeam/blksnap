#ifndef BLK_SNAP_UTILS_HELPER_H
#define BLK_SNAP_UTILS_HELPER_H

#include <blk-snap/types.h>
#include <sys/types.h>
#include <sys/sysmacros.h>

struct ioctl_dev_id_s to_dev_id(dev_t dev)
{
    struct ioctl_dev_id_s result;
    result.major = major(dev);
    result.minor = minor(dev);

    return result;
}

#endif //BLK_SNAP_UTILS_HELPER_H
