//@todo: hide this header
#ifndef BLK_SNAP_HELPER_H
#define BLK_SNAP_HELPER_H

#include <blk-snap/snapshot_ctl.h>
#include <linux/fiemap.h>
#include <string>
#include <sys/stat.h>
#include <vector>

class Helper
{
public:
    static dev_t GetDevice(const std::string& devPath);
    static dev_t GetDeviceForFile(const std::string& file);
    static ioctl_dev_id_s ToDevId(dev_t dev);
    static std::vector<fiemap_extent> Fiemap(const std::string& file);
};

#endif // BLK_SNAP_HELPER_H
