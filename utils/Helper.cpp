#include "Helper.h"
#include <system_error>

dev_t Helper::GetDevice(const std::string& devPath)
{
    struct stat stats;
    if (stat(devPath.c_str(), &stats))
        throw std::system_error(errno, std::generic_category(), "Failed to get device stat");

    //@todo add check BLK_DEV

    return stats.st_rdev;
}
