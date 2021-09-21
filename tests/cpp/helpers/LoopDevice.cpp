#include "LoopDevice.h"

#include <boost/process.hpp>
#include <boost/algorithm/string.hpp>
#include <iostream>

#include "Uuid.h"

boost::filesystem::path LoopDevice::GetDevice() const
{
    return m_loopDevice;
}

LoopDevice::LoopDevice(boost::filesystem::path image, boost::filesystem::path loop)
    : m_image(std::move(image))
    , m_loopDevice(loop)
{}

LoopDevice::Ptr LoopDevice::Create(boost::filesystem::path directory, size_t size)
{
    int fd = -1;
    boost::filesystem::path image = directory / (Uuid::GenerateRandom().ToStr() + std::string(".loop_image"));
    try
    {
        fd = open(image.c_str(), O_CREAT | O_EXCL | O_RDWR, S_IRWXU);
        if ( fd == -1 )
            throw std::system_error(errno, std::generic_category(), "Failed to create loop image file");

        if ( ::fallocate64(fd, 0, 0, size) )
            throw std::system_error(errno, std::generic_category(), "Failed to allocate loop image file");
    
        ::close(fd);
        return LoopDevice::Ptr(new LoopDevice(image, LoopSetup(image)));
    }
    catch ( std::exception& ex )
    {
        if ( fd != -1 )
            ::close(fd);

        boost::filesystem::remove(image);
        throw;
    }
}

void LoopDevice::LoopDetach(boost::filesystem::path path)
{
    boost::process::ipstream out, err;
    std::string command = std::string("losetup --detach ") + path.c_str();

    int res = boost::process::system(command, boost::process::std_out > out, boost::process::std_err > err,
                                     boost::process::std_in < boost::process::null);
    
    std::string loopPath((std::istreambuf_iterator<char>(out)), std::istreambuf_iterator<char>());
    std::string error((std::istreambuf_iterator<char>(err)), std::istreambuf_iterator<char>());

    if ( res != 0 )
        throw std::runtime_error(std::string("Failed to detach loop device: ") + error);
}

boost::filesystem::path LoopDevice::LoopSetup(boost::filesystem::path path)
{
    boost::process::ipstream out, err;
    std::string command = std::string("losetup -f --show ") + path.c_str();

    int res = boost::process::system(command, boost::process::std_out > out, boost::process::std_err > err,
                                     boost::process::std_in < boost::process::null);

    std::string loopPath((std::istreambuf_iterator<char>(out)), std::istreambuf_iterator<char>());
    std::string error((std::istreambuf_iterator<char>(err)), std::istreambuf_iterator<char>());

    if ( res != 0 )
        throw std::runtime_error(std::string("Failed to setup loop device: \n") + error);

    boost::trim(loopPath);
    return loopPath;
}

void LoopDevice::Mkfs(const std::string fsType)
{
    boost::process::ipstream out, err;
    std::string command = std::string("mkfs.") + fsType + " " + m_loopDevice.string();
    
    int res = boost::process::system(command, boost::process::std_out > out, boost::process::std_err > err,
                                     boost::process::std_in < boost::process::null);
    
    std::string loopPath((std::istreambuf_iterator<char>(out)), std::istreambuf_iterator<char>());
    std::string error((std::istreambuf_iterator<char>(err)), std::istreambuf_iterator<char>());
    
    if ( res != 0 )
        throw std::runtime_error(std::string("Failed to mkfs in loop device: \n") + error);
}

LoopDevice::~LoopDevice()
{
    try
    {
        LoopDetach(m_loopDevice);
        boost::filesystem::remove(m_image);
    }
    catch ( std::exception& ex )
    {
        std::cerr << ex.what() << std::endl;
    }
}
