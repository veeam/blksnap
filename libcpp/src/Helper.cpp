#include <blk-snap-cpp/Helper.h>
#include <boost/filesystem.hpp>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <system_error>
#include <unistd.h>

#include "FdHolder.h"

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

dev_t Helper::GetDeviceForFile(const std::string& file)
{
    struct stat stats;
    if ( stat(file.c_str(), &stats) )
        throw std::system_error(errno, std::generic_category(), "Failed to get file stat");

    //@todo add check REG_FILE

    return stats.st_dev;
}

std::vector<fiemap_extent> Helper::Fiemap(const std::string& file)
{
    std::vector<fiemap_extent> extends;
    int fd = ::open(file.c_str(), O_RDWR);
    if ( fd == -1 )
        throw std::system_error(errno, std::generic_category(), "Failed to open file");

    FdHolder fdHolder(fd);
    
    uint32_t ignoreMask = (FIEMAP_EXTENT_LAST | FIEMAP_EXTENT_UNWRITTEN | FIEMAP_EXTENT_MERGED);
    const uint32_t extentsPerQuery = 500;
    std::vector<uint8_t> fiemapBuffer(sizeof(fiemap) + sizeof(fiemap_extent) * extentsPerQuery);
    size_t fileSize = boost::filesystem::file_size(file);
    fiemap* pFiemap = reinterpret_cast<fiemap*>(&fiemapBuffer.front());

    for ( uint64_t logicalOffset = 0; logicalOffset < fileSize; )
    {
        pFiemap->fm_start = logicalOffset;
        pFiemap->fm_length = fileSize - logicalOffset;
        pFiemap->fm_extent_count = extentsPerQuery;
        pFiemap->fm_flags = 0;

        if ( ::ioctl(fd, FS_IOC_FIEMAP, pFiemap) )
            throw std::system_error(errno, std::generic_category(), "Failed to call FIEMAP");

        for ( uint32_t i = 0; i != pFiemap->fm_mapped_extents; ++i )
        {
            const fiemap_extent& fiemapExtent = pFiemap->fm_extents[i];
            if ( fiemapExtent.fe_flags & ~ignoreMask )
                throw std::runtime_error("Incompatible extent flags"); //@todo add more information

            extends.push_back(fiemapExtent);
            logicalOffset = fiemapExtent.fe_logical + fiemapExtent.fe_length;
        }

        if ( pFiemap->fm_mapped_extents != extentsPerQuery )
            break;
    }

    return extends;
}
