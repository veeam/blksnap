#ifndef BLK_SNAP_UTILS_HELPER_H
#define BLK_SNAP_UTILS_HELPER_H

#include <blk-snap/types.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sstream>
#include <iomanip>

struct ioctl_dev_id_s to_dev_id(dev_t dev)
{
    struct ioctl_dev_id_s result;
    result.major = major(dev);
    result.minor = minor(dev);

    return result;
}

std::string snap_id_to_str(const unsigned char* id)
{
    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(2);
    for (size_t i = 0; i < SNAP_ID_LENGTH; ++i )
        ss << (int)id[i];

    return ss.str();
}

void str_to_snap_id(std::string& str, unsigned char* id)
{
    int val;
    for (size_t i = 0; i < SNAP_ID_LENGTH; ++i)
    {
        std::stringstream ss(str.substr(i*2, 2));
        ss >> std::hex >> val;
        id[i] = val;
    }
}

std::string snap_store_to_str(struct snap_store& store)
{
    return snap_id_to_str(store.id);
}


#endif //BLK_SNAP_UTILS_HELPER_H
