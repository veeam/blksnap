#include <iostream>
#include <system_error>
#include <blk-snap/snapshot_ctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <algorithm>


struct cbt_info_s get_cbt_info(struct snap_ctx* snapCtx, dev_t dev)
{
    cbt_info_s result;
    unsigned int length = 0;
    if (snap_get_tracking(snapCtx, NULL, &length) != 0)
        throw std::system_error(errno, std::generic_category(), "Failed to get tracking length");

    if (length == 0)
        throw std::runtime_error("No device found under tracking");

    struct cbt_info_s* cbtInfos = (struct cbt_info_s*)malloc(sizeof(struct cbt_info_s) * length);
    if (snap_get_tracking(snapCtx, cbtInfos, &length) != 0)
    {
        free(cbtInfos);
        throw std::system_error(errno, std::generic_category(), "Failed to get tracking");
    }

    for (unsigned int i = 0; i < length; i++)
    {
        if (minor(dev) == cbtInfos[i].dev_id.minor && major(dev) == cbtInfos[i].dev_id.major)
        {
            result = cbtInfos[i];
            free(cbtInfos);
            return result;
        }
    }

    free(cbtInfos);
    throw std::runtime_error("Device not found under tracking");
}

size_t GetChanged(const unsigned char* bitmap, size_t length)
{
    size_t count = 0;
    for (size_t i = 0 ; i < length; ++i)
    {
        unsigned char byte = bitmap[i];
        while (byte)
        {
            count += byte & 1;
            byte >>= 1;
        }
    }

    return count;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
        throw std::runtime_error("need dev path");

    struct snap_ctx* snapCtx;
    if (snap_ctx_create(&snapCtx) != 0)
        throw std::system_error(errno, std::generic_category(), "Failed to create snap context");

    struct stat dev_stat;
    stat(argv[1], &dev_stat);

    cbt_info_s cbtInfo = get_cbt_info(snapCtx, dev_stat.st_rdev);

    size_t count = 0;
    unsigned char CBT_MAP[32 * 1024];
    unsigned int offset = 0;
    while (offset < cbtInfo.cbt_map_size)
    {
        int portion_length = std::min((size_t)cbtInfo.cbt_map_size - offset, sizeof(CBT_MAP));
        int result = snap_read_cbt(snapCtx, dev_stat.st_rdev, offset, portion_length, CBT_MAP);
        if (result < 0)
            throw std::system_error(errno, std::generic_category(), "Failed to read cbt");
        offset += result;
        count += GetChanged(CBT_MAP, result);

    }

    std::cout << "All: " << cbtInfo.cbt_map_size << std::endl;
    std::cout << "Changed: " << count << std::endl;
}


