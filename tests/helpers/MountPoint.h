#pragma once

#include <boost/filesystem.hpp>

class MountPoint
{
public:
    MountPoint(boost::filesystem::path device, boost::filesystem::path mountPoint);
    ~MountPoint();
    
    boost::filesystem::path GetDevice() const;
    boost::filesystem::path GetMountPoint() const;
    
private:
    void Mount();
    void UnMount();
    
private:
    boost::filesystem::path m_device;
    boost::filesystem::path m_mountPoint;
};
