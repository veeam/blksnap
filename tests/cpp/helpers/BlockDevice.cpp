#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/fs.h>

#include <system_error>
#include "BlockDevice.h"

CBlockDevice::CBlockDevice(const std::string &name, const bool isSync)
    : m_name(name)
    , m_fd(0)
{
    unsigned int flags = isSync ? O_SYNC | O_DSYNC : 0;

    m_fd = ::open(m_name.c_str(), O_RDWR | O_DIRECT | flags);
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
    ssize_t ret = ::pread(m_fd, buf, count, offset);
    if (ret < 0)
        throw std::system_error(errno, std::generic_category(), "Failed to read block device");
    if (ret < count)
        throw std::runtime_error("Reading outside the boundaries of a block device");
};

void CBlockDevice::Write(const void *buf, size_t count, off_t offset)
{
    ssize_t ret = ::pwrite(m_fd, buf, count, offset);
    if (ret < 0)
        throw std::system_error(errno, std::generic_category(), "Failed to write block device");
    if (ret < count)
        throw std::runtime_error("Writing outside the boundaries of a block device");
};

off_t CBlockDevice::Size()
{
    off_t sz;

    if (::ioctl(m_fd, BLKGETSIZE64, &sz) == -1)
        throw std::system_error(errno, std::generic_category(),
            "Failed to get block device size");

    return sz;
};

const std::string& CBlockDevice::Name()
{
    return m_name;
};
