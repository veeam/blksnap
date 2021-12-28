#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/fs.h>

#include <system_error>
#include "BlockDevice.h"

CBlockDevice::CBlockDevice(const std::string &name)
    : m_name(name)
    , m_fd(0)
{
    m_fd = ::open(m_name.c_str(), O_RDWR | O_DIRECT);
    if (m_fd < 0)
        throw std::system_error(errno, std::generic_category(),
            "Failed to open file '" + m_name + "'.");
};

CBlockDevice::~CBlockDevice()
{
    if (m_fd) {
        ::close(m_fd);
        m_fd = 0;
    }
};

void CBlockDevice::Read(void *buf, size_t count, off_t offset)
{
    ssize_t ret;

    ret = ::pread(m_fd, buf, count, offset);
    if (ret < 0)
        throw std::system_error(errno, std::generic_category(), "Failed to read block device.");
    if (ret < count)
        throw std::runtime_error("Reading outside the boundaries of a block device.");
};

void CBlockDevice::Write(void *buf, size_t count, off_t offset)
{
    ssize_t ret;

    ret = ::pwrite(m_fd, buf, count, offset);
    if (ret < 0)
        throw std::system_error(errno, std::generic_category(), "Failed to read block device.");
    if (ret < count)
        throw std::runtime_error("Writing outside the boundaries of a block device.");
};

off_t CBlockDevice::Size()
{
    off_t sz;

    if (::ioctl(m_fd, BLKGETSIZE64, &sz) == -1)
        throw std::system_error(errno, std::generic_category(),
            "Failed to get block device size");

    return sz;
};
