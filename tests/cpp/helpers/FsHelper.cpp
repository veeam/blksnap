// SPDX-License-Identifier: GPL-2.0+
#include "FsHelper.h"

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <system_error>
#include <unistd.h>

CAllocatedFile::CAllocatedFile(const std::string& name, const off_t size)
    : m_name(name)
    , m_size(size)
{
        int fd = ::open(filename.c_str(), O_CREAT | O_RDWR | O_EXCL | O_LARGEFILE);
        if (fd < 0)
            throw std::system_error(errno, std::generic_category(),
                                    "Failed to create file for diff storage.");

        if (::fallocate64(fd, 0, 0, m_size))
        {
            int err = errno;

            ::close(fd);
            throw std::system_error(err, std::generic_category(), "Failed to allocate file for diff storage.");
        }

        ::close(fd);
};

CAllocatedFile::~CAllocatedFile()
{};

const CAllocatedFile::std::string& Name()
{
    return m_name;
}

off_t CAllocatedFile::Size()
{
    return m_size;
}

void CAllocatedFile::Location(dev_t& dev_id, std::vector<BlockRange>& ranges)
{
    int ret = 0;
    const char* errMessage;
    int fd = -1;
    struct fiemap* map = NULL;
    int extentMax = 500;
    long long fileSize;
    struct stat64 st;

    if (::stat64(m_name.c_str(), &st))
        throw std::system_error(errno, std::generic_category(), "Failed to get file size.");

    dev_id = st.st_dev;
    fileSize = st.st_size;

    ranges.clear();

    fd = ::open(filename.c_str(), O_RDONLY | O_EXCL | O_LARGEFILE);
    if (fd < 0)
    {
        ret = errno;
        errMessage = "Failed to open file.";
        goto out;
    }

    map = (struct fiemap*)::malloc(sizeof(struct fiemap) + sizeof(struct fiemap_extent) * extentMax);
    if (!map)
    {
        ret = ENOMEM;
        errMessage = "Failed to allocate memory for fiemap structure.";
        goto out;
    }

    for (long long fileOffset = 0; fileOffset < fileSize;)
    {
        map->fm_start = fileOffset;
        map->fm_length = fileSize - fileOffset;
        map->fm_extent_count = extentMax;
        map->fm_flags = 0;

        if (::ioctl(fd, FS_IOC_FIEMAP, map))
        {
            ret = errno;
            errMessage = "Failed to call FS_IOC_FIEMAP.";
            goto out;
        }

        for (int i = 0; i < map->fm_mapped_extents; ++i)
        {
            struct fiemap_extent* extent = map->fm_extents + i;

            if (extent->fe_physical & (SECTOR_SIZE - 1))
            {
                ret = EINVAL;
                errMessage = "File location is not ordered by sector size.";
                goto out;
            }

            ranges.emplace_back(extent->fe_physical, extent->fe_length);

            fileOffset = extent->fe_logical + extent->fe_length;

            //std::cout << "allocate range: ofs=" << rg.sector_offset << " cnt=" << rg.sector_count << std::endl;
        }
    }

out:
    if (map)
        ::free(map);
    if (fd >= 0)
        ::close(fd);
    if (ret)
        throw std::system_error(ret, std::generic_category(), errMessage);
}
