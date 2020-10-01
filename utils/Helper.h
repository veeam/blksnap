#ifndef BLK_SNAP_HELPER_H
#define BLK_SNAP_HELPER_H

#include <sys/stat.h>
#include <string>

class Helper
{
public:
    static dev_t GetDevice(const std::string& devPath);

};

#endif // BLK_SNAP_HELPER_H
