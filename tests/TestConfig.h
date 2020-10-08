#pragma once

#include <string>
#include <boost/filesystem.hpp>
#include <blk-snap-cpp/Uuid.h>

struct TestConfig
{
    std::string snap_image_name;
    boost::filesystem::path test_dir;
    boost::filesystem::path mount_dir;
    boost::filesystem::path test_device;

    static const TestConfig& Get();
    static void Set(const TestConfig&);
};
