#include <blk-snap-cpp/BlkSnapStoreCtx.h>
#include <blk-snap-cpp/FileSnapshotStore.h>
#include <blk-snap-cpp/Helper.h>
#include <catch2/catch.hpp>

#include "helpers/FileHelper.h"
#include "CommonSnapStoreTest.h"
#include "TestConfig.h"

TEST_CASE("file snap store", "[snapshot]")
{
    boost::filesystem::path fileSnapStorage = TestConfig::Get().workDir_dir/"file_snap";
    FileHelper::Create(fileSnapStorage, 500 * 1024 * 1024);
    
    BlkSnapCtx::Ptr ptrSnapCtx = std::make_shared<BlkSnapCtx>();

    std::vector<dev_t> snapDevs;
    REQUIRE_NOTHROW(snapDevs.push_back(Helper::GetDevice(TestConfig::Get().test_device.string())));
    
    std::shared_ptr<FileSnapshotStore> ptrStoreCtx;
    REQUIRE_NOTHROW(ptrStoreCtx.reset(new FileSnapshotStore(ptrSnapCtx, snapDevs, fileSnapStorage.string())));
    
    CommonSnapStoreTest(TestConfig::Get().test_dir/"file_snap_store", TestConfig::Get().test_device, ptrStoreCtx->GetSnapStoreCtx());
}