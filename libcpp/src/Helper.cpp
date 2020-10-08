#include "Helper.h"

#include <sys/sysmacros.h>
#include <system_error>

dev_t Helper::GetDevice(const std::string& devPath)
{
    struct stat stats;
    if ( stat(devPath.c_str(), &stats) )
        throw std::system_error(errno, std::generic_category(), "Failed to get device stat");

    //@todo add check BLK_DEV

    return stats.st_rdev;
}

ioctl_dev_id_s Helper::ToDevId(dev_t dev)
{
    struct ioctl_dev_id_s result;
    result.major = major(dev);
    result.minor = minor(dev);

    return result;
}
