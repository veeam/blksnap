#include <blk-snap-cpp/BlkSnapStoreCtx.h>
#include <blk-snap-cpp/Snapshot.h>
#include <blk-snap-cpp/Helper.h>
#include <catch2/catch.hpp>

#include "helpers/LoopDevice.h"
#include "CommonSnapStoreTest.h"

static LoopDevice::Ptr GetLoopDevice()
{
    LoopDevice::Ptr ptrLoop;
    ptrLoop = LoopDevice::Create("/tmp", 1024 * 1024 * 1024);
    ptrLoop->Mkfs("ext4");

    return ptrLoop;
}

TEST_CASE("inmemory", "[snapshot]")
{
    Uuid uuid = Uuid::GenerateRandom();
    
    boost::filesystem::path testDir = boost::filesystem::path("/tmp/snap_test/") / uuid.ToStr() / "inmemory";
    boost::filesystem::create_directories(testDir);
    
    
    LoopDevice::Ptr ptrLoop = GetLoopDevice();
    BlkSnapCtx::Ptr ptrSnapCtx = std::make_shared<BlkSnapCtx>();

    std::vector<dev_t> snapDevs;
    REQUIRE_NOTHROW(snapDevs.push_back(Helper::GetDevice(ptrLoop->GetDevice().string())));
    
    std::shared_ptr<BlkSnapStoreCtx> ptrStoreCtx;
    REQUIRE_NOTHROW(ptrStoreCtx.reset(new BlkSnapStoreCtx(BlkSnapStoreCtx::CreateInMemory(ptrSnapCtx, 500 * 1024 * 1024, snapDevs))));
    
//    std::shared_ptr<Snapshot> ptrSnapshot;
//    REQUIRE_NOTHROW(ptrSnapshot.reset(new Snapshot(Snapshot::Create(*ptrStoreCtx, snapDevs.front()))));
    
    CommonSnapStoreTest(testDir, ptrLoop->GetDevice(), *ptrStoreCtx);
}