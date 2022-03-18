// SPDX-License-Identifier: GPL-2.0+
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
    catch (std::exception& ex)
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

    if (res != 0)
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

    if (res != 0)
        throw std::runtime_error(std::string("Failed to umount device: ") + error);
}
