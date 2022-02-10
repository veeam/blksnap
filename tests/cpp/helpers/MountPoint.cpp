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
#include "MountPoint.h"

#include <boost/process.hpp>
#include <iostream>

MountPoint::MountPoint(boost::filesystem::path device, boost::filesystem::path mountPoint)
    : m_device(device)
    , m_mountPoint(mountPoint)
{
    Mount();
}

MountPoint::~MountPoint()
{
    try
    {
        UnMount();
    }
    catch ( std::exception& ex )
    {
        std::cerr << "Failed to unmount " << m_device << ". Mount point " << m_mountPoint << std::endl;
        std::cerr << ex.what() << std::endl;
    }
}

boost::filesystem::path MountPoint::GetDevice() const
{
    return m_device;
}

boost::filesystem::path MountPoint::GetMountPoint() const
{
    return m_mountPoint;
}

void MountPoint::Mount()
{
    if (!boost::filesystem::exists(m_mountPoint))
        boost::filesystem::create_directories(m_mountPoint);

    boost::process::ipstream out, err;
    std::string command = std::string("mount ") + m_device.string() + " " + m_mountPoint.string();

    int res = boost::process::system(command, boost::process::std_out > out, boost::process::std_err > err,
                                     boost::process::std_in < boost::process::null);

    std::string loopPath((std::istreambuf_iterator<char>(out)), std::istreambuf_iterator<char>());
    std::string error((std::istreambuf_iterator<char>(err)), std::istreambuf_iterator<char>());

    if ( res != 0 )
        throw std::runtime_error(std::string("Failed to mount device: ") + error);
}

void MountPoint::UnMount()
{
    boost::process::ipstream out, err;
    std::string command = std::string("umount ") + m_device.string();

    int res = boost::process::system(command, boost::process::std_out > out, boost::process::std_err > err,
                                     boost::process::std_in < boost::process::null);

    std::string loopPath((std::istreambuf_iterator<char>(out)), std::istreambuf_iterator<char>());
    std::string error((std::istreambuf_iterator<char>(err)), std::istreambuf_iterator<char>());

    if ( res != 0 )
        throw std::runtime_error(std::string("Failed to umount device: ") + error);
}
