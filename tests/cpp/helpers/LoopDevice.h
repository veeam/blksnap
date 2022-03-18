// SPDX-License-Identifier: GPL-2.0+
#pragma once

#include <boost/filesystem.hpp>
#include <memory>

class LoopDevice
{
public:
    using Ptr = std::shared_ptr<LoopDevice>;

    ~LoopDevice();
    static LoopDevice::Ptr Create(boost::filesystem::path directory, size_t size);

    boost::filesystem::path GetDevice() const;
    void Mkfs(const std::string fsType = "ext4");

private:
    LoopDevice(boost::filesystem::path image, boost::filesystem::path loop);

    static boost::filesystem::path LoopSetup(boost::filesystem::path path);
    static void LoopDetach(boost::filesystem::path path);

private:
    boost::filesystem::path m_image;
    boost::filesystem::path m_loopDevice;
};
