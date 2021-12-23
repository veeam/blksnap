#include "BlockDevice.h"
#include <unistd.h>
#include <linux/fs.h>
...

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

    ret = ::pread();
};

off_t CBlockDevice::Size()
{
    off_t sz;

    if (::ioctl(file, BLKGETSIZE64, &sz) == -1)
        throw std::system_error(errno, std::generic_category(),
            "Failed to get block device size");

    return sz;
};
