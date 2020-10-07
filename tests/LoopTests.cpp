#include <catch2/catch.hpp>
#include "helpers/LoopDevice.h"
#include "helpers/MountPoint.h"
#include "helpers/FileHelper.h"
#include <iostream>

TEST_CASE("create loop", "[loop]")
{
    LoopDevice::Ptr ptrLoop;
    REQUIRE_NOTHROW(ptrLoop = LoopDevice::Create("/tmp", 1024 * 1024 * 1024));
    REQUIRE_NOTHROW(ptrLoop->Mkfs("ext4"));
    
    MountPoint mountPoint(ptrLoop->GetDevice(), "/tmp/mount_test");
    boost::filesystem::path file = mountPoint.GetMountPoint() / "file";
    
    REQUIRE_NOTHROW(FileHelper::Create(file, 1024 * 1024));
    REQUIRE_NOTHROW(FileHelper::FillRandom(file));
    
    boost::filesystem::path file2 = mountPoint.GetMountPoint() / "file2";
    REQUIRE_NOTHROW(FileHelper::Create(file2, 1024 * 1024));
    REQUIRE_NOTHROW(FileHelper::FillRandom(file2));
    
    REQUIRE(FileHelper::CalcHash(file2) != FileHelper::CalcHash(file));
}
