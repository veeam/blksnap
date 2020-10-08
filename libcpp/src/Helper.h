#ifndef BLK_SNAP_HELPER_H
#define BLK_SNAP_HELPER_H

#include <sys/stat.h>
#include <string>
#include <blk-snap/snapshot_ctl.h>

class Helper
{
public:
    static dev_t GetDevice(const std::string& devPath);
    static ioctl_dev_id_s ToDevId(dev_t dev);

};

#endif // BLK_SNAP_HELPER_H
