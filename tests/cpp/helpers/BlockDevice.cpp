/* [TBD]
 * Copyright (C) 2022 Veeam Software Group GmbH <https://www.veeam.com/contacts.html>
 *
 * This file is part of blksnap-tests
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
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

    m_fd = ::open(m_name.c_str(), O_RDWR | O_DIRECT | flags);
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

const std::string& CBlockDevice::Name()
{
    return m_name;
};
