#include "CommonSnapStoreTest.h"

#include <blk-snap-cpp/BlkSnapStoreCtx.h>
#include <blk-snap-cpp/Helper.h>
#include <blk-snap-cpp/Snapshot.h>
#include <boost/filesystem.hpp>
#include <catch2/catch.hpp>

#include "helpers/MountPoint.h"
#include "helpers/SnapshotHelper.h"
#include "helpers/FileHelper.h"
#include "TestConfig.h"

#define RANDOM_DATA_SIZE 500*1024

void CommonSnapStoreTest(boost::filesystem::path testName, boost::filesystem::path originalDev,
                         BlkSnapStoreCtx& storeCtx)
{
    boost::filesystem::path commonTestMountDir = TestConfig::Get().workDir_dir /testName;

    boost::filesystem::path origMountDir = commonTestMountDir/"orig_mount";
    boost::filesystem::path snapMountDir = commonTestMountDir/"snap_mount";

    MountPoint origMountPoint(originalDev, origMountDir);
    boost::filesystem::create_directories(origMountDir/TestConfig::Get().test_dir/testName);
    boost::filesystem::path origFile = origMountDir/TestConfig::Get().test_dir/testName/"file";
    FileHelper::Create(origFile, RANDOM_DATA_SIZE);
    FileHelper::FillRandom(origFile);
    std::string origHash = FileHelper::CalcHash(origFile);
    
    std::shared_ptr<Snapshot> ptrSnapshot;
    int minor = 0;
    REQUIRE_NOTHROW(ptrSnapshot.reset(new Snapshot(Snapshot::Create(storeCtx, Helper::GetDevice(originalDev.string())))));
    REQUIRE_NOTHROW(minor = SnapshotHelper::GetSnapshotDevice(storeCtx.GetBlkSnapCtx(), originalDev));
    
    boost::filesystem::path snapFile = snapMountDir/TestConfig::Get().test_dir/testName/"file";
    MountPoint snapMountPoint(TestConfig::Get().snap_image_name + std::to_string(minor), snapMountDir);
    std::string snapHash = FileHelper::CalcHash(snapFile);
    
    REQUIRE(snapHash == origHash);
    FileHelper::FillRandom(origFile);
    origHash = FileHelper::CalcHash(origFile);
    REQUIRE(snapHash != origHash);
    std::string snapHash2 = FileHelper::CalcHash(snapFile);
    REQUIRE(snapHash == snapHash2);
    boost::filesystem::remove(origFile);
}