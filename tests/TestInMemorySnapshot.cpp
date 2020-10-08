#include <blk-snap-cpp/BlkSnapStoreCtx.h>
#include <blk-snap-cpp/Snapshot.h>
#include <blk-snap-cpp/Helper.h>
#include <catch2/catch.hpp>

#include "helpers/LoopDevice.h"
#include "CommonSnapStoreTest.h"
#include "TestConfig.h"

static LoopDevice::Ptr GetLoopDevice()
{
    LoopDevice::Ptr ptrLoop;
    ptrLoop = LoopDevice::Create("/tmp", 1024 * 1024 * 1024);
    ptrLoop->Mkfs("ext4");

    return ptrLoop;
}

TEST_CASE("inmemory", "[snapshot]")
{
    BlkSnapCtx::Ptr ptrSnapCtx = std::make_shared<BlkSnapCtx>();

    std::vector<dev_t> snapDevs;
    REQUIRE_NOTHROW(snapDevs.push_back(Helper::GetDevice(TestConfig::Get().test_device.string())));
    
    std::shared_ptr<BlkSnapStoreCtx> ptrStoreCtx;
    REQUIRE_NOTHROW(ptrStoreCtx.reset(new BlkSnapStoreCtx(BlkSnapStoreCtx::CreateInMemory(ptrSnapCtx, 500 * 1024 * 1024, snapDevs))));
    
    CommonSnapStoreTest(TestConfig::Get().test_dir/"inmemory", TestConfig::Get().test_device, *ptrStoreCtx);
}