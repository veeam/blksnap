#include <iostream>
#include <system_error>
#include <blk-snap/snapshot_ctl.h>
#include <sys/stat.h>
#include <linux/fiemap.h>
#include <vector>
#include <sys/ioctl.h>
#include "helper.h"
#include <linux/fs.h>
#include <fcntl.h>

size_t GetFileSize(int file)
{
    struct stat64 file_stat;
    int  stat_res = fstat64( file, &file_stat );
    if ( 0 != stat_res )
        throw std::system_error(errno, std::generic_category(), "Failed to get file size");

    size_t result = (file_stat.st_size/file_stat.st_blksize) * file_stat.st_blksize;
    if (result == 0)
        throw std::runtime_error("No blocks in file");

    return result;
}

std::vector<struct ioctl_range_s> EnumRanges(int file)
{
    std::vector<fiemap_extent> extends;

    uint32_t ignoreMask = (FIEMAP_EXTENT_LAST | FIEMAP_EXTENT_UNWRITTEN | FIEMAP_EXTENT_MERGED);
    const uint32_t extentsPerQuery = 500;
    std::vector<uint8_t> fiemapBuffer(sizeof(fiemap) + sizeof(fiemap_extent) * extentsPerQuery);
    size_t fileSize = GetFileSize(file);
    fiemap* pFiemap = reinterpret_cast<fiemap*>(&fiemapBuffer.front());

    for (uint64_t logicalOffset = 0; logicalOffset < fileSize; )
    {
        pFiemap->fm_start = logicalOffset;
        pFiemap->fm_length = fileSize - logicalOffset;
        pFiemap->fm_extent_count = extentsPerQuery;
        pFiemap->fm_flags = 0;

        if (::ioctl(file, FS_IOC_FIEMAP, pFiemap))
            throw std::system_error(errno, std::generic_category(), "Failed to call FIEMAP");

        for (uint32_t i = 0; i != pFiemap->fm_mapped_extents; ++i)
        {
            const fiemap_extent& fiemapExtent = pFiemap->fm_extents[i];
            if (fiemapExtent.fe_flags & ~ignoreMask)
                throw std::runtime_error("Incompatible extent flags"); //@todo add more information

            extends.push_back(fiemapExtent);
            logicalOffset = fiemapExtent.fe_logical + fiemapExtent.fe_length;
        }

        if (pFiemap->fm_mapped_extents != extentsPerQuery)
            break;
    }

    std::vector<struct ioctl_range_s> result;
    result.reserve(extends.size());
    for (auto& ext : extends)
    {
        struct ioctl_range_s st;
        st.left = ext.fe_physical;
        st.right = ext.fe_physical + ext.fe_length;
        result.push_back(st);
    }

    return result;
}


int main(int argc, char *argv[])
{
    if (argc < 3)
        throw std::runtime_error("need file path");

    struct snap_ctx* snapCtx;
    if (snap_ctx_create(&snapCtx) != 0)
        throw std::system_error(errno, std::generic_category(), "Failed to create snap context");

    struct stat snapDev;
    stat(argv[1], &snapDev);
    struct ioctl_dev_id_s snapDevId = to_dev_id(snapDev.st_rdev);

    int fd = open(argv[2], O_RDWR);
    if (fd == -1)
        throw std::system_error(errno, std::generic_category(), "Failed to open file");

    struct stat snapStoreDev;
    fstat(fd, &snapStoreDev);
    struct ioctl_dev_id_s snapStoreDevId = to_dev_id(snapStoreDev.st_dev);
    struct snap_store* snapStoreCtx = snap_create_snapshot_store(snapCtx, snapStoreDevId, snapDevId);

    std::cout << "Successfully create snapshot store: " << snap_store_to_str(snapStoreCtx) << std::endl;
    std::vector<struct ioctl_range_s> ranges = EnumRanges(fd);
    if (snap_create_file_snapshot_store(snapCtx, snapStoreCtx, ranges.data(), ranges.size()))
        throw std::system_error(errno, std::generic_category(), "Failed to create file snapshot store");

    unsigned long long snapshotId = snap_create_snapshot(snapCtx, snapDevId);
    if (snapshotId == 0)
        throw std::system_error(errno, std::generic_category(), "Failed to create snapshot");

    snap_store_ctx_free(snapStoreCtx);
    snap_ctx_destroy(snapCtx);
    std::cout << "Successfully create file snapshot id: " << snapshotId << "." << std::endl;
}



