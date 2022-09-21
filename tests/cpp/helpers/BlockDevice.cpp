// SPDX-License-Identifier: GPL-2.0+
#include "BlockDevice.h"

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <system_error>
#include <unistd.h>

CBlockDevice::CBlockDevice(const std::string& name, const bool isSync, const off_t size)
    : m_name(name)
    , m_size(size)
    , m_fd(0)
{
    unsigned int flags = isSync ? O_SYNC | O_DSYNC : 0;

    m_fd = ::open(m_name.c_str(), O_EXCL | O_RDWR | O_DIRECT | flags);
    if (m_fd < 0)
        throw std::system_error(errno, std::generic_category(), "Failed to open file '" + m_name + "'.");
};

CBlockDevice::~CBlockDevice()
{
    if (m_fd)
    {
        ::close(m_fd);
        m_fd = 0;
    }
};

void CBlockDevice::Read(void* buf, size_t count, off_t offset)
{
    ssize_t ret = ::pread(m_fd, buf, count, offset);
    if (ret < 0)
        throw std::system_error(errno, std::generic_category(), "Failed to read block device. offset=" + std::to_string(offset) + " size=" + std::to_string(count));
    if (ret < count)
        throw std::runtime_error("Reading outside the boundaries of a block device");
};

void CBlockDevice::Write(const void* buf, size_t count, off_t offset)
{
    ssize_t ret = ::pwrite(m_fd, buf, count, offset);
    if (ret < 0)
        throw std::system_error(errno, std::generic_category(), "Failed to write block device. offset=" + std::to_string(offset) + " size=" + std::to_string(count));
    if (ret < count)
        throw std::runtime_error("Writing outside the boundaries of a block device");
};

off_t CBlockDevice::Size()
{
    if (m_size == 0)
        if (::ioctl(m_fd, BLKGETSIZE64, &m_size) == -1)
            throw std::system_error(errno, std::generic_category(), "Failed to get block device size");

    return m_size;
};

size_t CBlockDevice::BlockSize()
{
    size_t size = 0;
    /*
     * On the arm64 platform the BLKBSZGET call can returns a value only in
     * lower 32-bit digits.
     * Therefore, the size must be set to zero before calling on le arch.
     * In the big endian systems, this code may have problems.
     * It looks like something is wrong for the case when CONFIG_COMPAT is
     * enabled.
     */
    if (::ioctl(m_fd, BLKBSZGET, &size) == -1)
        throw std::system_error(errno, std::generic_category(), "Failed to get block size for device");

    return size;
}

const std::string& CBlockDevice::Name()
{
    return m_name;
};
