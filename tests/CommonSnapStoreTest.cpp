#include "CommonSnapStoreTest.h"

#include <blk-snap-cpp/BlkSnapStoreCtx.h>
#include <blk-snap-cpp/Helper.h>
#include <blk-snap-cpp/Snapshot.h>
#include <boost/filesystem.hpp>
#include <catch2/catch.hpp>

#include "helpers/MountPoint.h"
#include "helpers/SnapshotHelper.h"
#include "helpers/FileHelper.h"

#define RANDOM_DATA_SIZE 500*1024

void CommonSnapStoreTest(boost::filesystem::path testDir, boost::filesystem::path originalDev,
                         BlkSnapStoreCtx& storeCtx)
{
    boost::filesystem::path commonTestDir = testDir / "common_test";
    boost::filesystem::create_directory(commonTestDir);

    boost::filesystem::path origDir = commonTestDir / "orig";
    boost::filesystem::path snapDir = commonTestDir / "snap";

    MountPoint origMountPoint(originalDev, origDir);
    boost::filesystem::path origFile = origDir / "file";
    FileHelper::Create(origFile, RANDOM_DATA_SIZE);
    FileHelper::FillRandom(origFile);
    std::string origHash = FileHelper::CalcHash(origFile);
    
    std::shared_ptr<Snapshot> ptrSnapshot;
    int minor = 0;
    REQUIRE_NOTHROW(ptrSnapshot.reset(new Snapshot(Snapshot::Create(storeCtx, Helper::GetDevice(originalDev.string())))));
    REQUIRE_NOTHROW(minor = SnapshotHelper::GetSnapshotDevice(storeCtx.GetBlkSnapCtx(), originalDev));
    
    boost::filesystem::path snapFile = snapDir / "file";
    MountPoint snapMountPoint(std::string("/dev/veeamimage") + std::to_string(minor), snapDir);
    std::string snapHash = FileHelper::CalcHash(snapFile);
    
    REQUIRE(snapHash == origHash);
    FileHelper::FillRandom(origFile);
    origHash = FileHelper::CalcHash(origFile);
    REQUIRE(snapHash != origHash);
    std::string snapHash2 = FileHelper::CalcHash(snapFile);
    REQUIRE(snapHash == snapHash2);
}