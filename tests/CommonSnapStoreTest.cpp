#include "CommonSnapStoreTest.h"

#include <blk-snap-cpp/BlkSnapStoreCtx.h>
#include <blk-snap-cpp/Helper.h>
#include <blk-snap-cpp/Snapshot.h>
#include <boost/filesystem.hpp>
#include <catch2/catch.hpp>

#include "helpers/MountPoint.h"

void CommonSnapStoreTest(boost::filesystem::path testDir, boost::filesystem::path originalDev,
                         BlkSnapStoreCtx& storeCtx)
{
    boost::filesystem::path commonTestDir = testDir / "common_test";
    boost::filesystem::create_directory(commonTestDir);

    boost::filesystem::path origDir = commonTestDir / "orig";
    boost::filesystem::path snapDir = commonTestDir / "snap";

    MountPoint mountPoint(originalDev, origDir);
    boost::filesystem::path file = origDir / "file";

    std::shared_ptr<Snapshot> ptrSnapshot;
    REQUIRE_NOTHROW(ptrSnapshot.reset(new Snapshot(Snapshot::Create(storeCtx, Helper::GetDevice(originalDev.string())))));
}