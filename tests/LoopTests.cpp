#include <catch2/catch.hpp>
#include "helpers/LoopDevice.h"
#include "helpers/MountPoint.h"
#include "helpers/FileHelper.h"
#include "TestConfig.h"

TEST_CASE("create loop", "[loop]")
{
    LoopDevice::Ptr ptrLoop;
    REQUIRE_NOTHROW(ptrLoop = LoopDevice::Create(TestConfig::Get().workDir_dir, 1024 * 1024 * 1024));
    REQUIRE_NOTHROW(ptrLoop->Mkfs("ext4"));
    
    MountPoint mountPoint(ptrLoop->GetDevice(), TestConfig::Get().workDir_dir/"loop_mount");
    boost::filesystem::path file = mountPoint.GetMountPoint() / "file";
    
    REQUIRE_NOTHROW(FileHelper::Create(file, 1024 * 1024));
    REQUIRE_NOTHROW(FileHelper::FillRandom(file));
    
    boost::filesystem::path file2 = mountPoint.GetMountPoint() / "file2";
    REQUIRE_NOTHROW(FileHelper::Create(file2, 1024 * 1024));
    REQUIRE_NOTHROW(FileHelper::FillRandom(file2));
    
    REQUIRE(FileHelper::CalcHash(file2) != FileHelper::CalcHash(file));
}
