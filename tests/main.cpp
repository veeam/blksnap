#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>
#include <blk-snap/snapshot_ctl.h>
#include "helpers/MountPoint.h"
#include "TestConfig.h"
#include "helpers/LoopDevice.h"

bool IsDiscovery(Catch::Session& sess)
{
    Catch::Config& cfg = sess.config();
    
    if (cfg.listTests() || cfg.listTestNamesOnly() || cfg.listTags() || cfg.listReporters() || cfg.showHelp())
        return true;
    
    return false;
}

void Clean()
{
    try
    {
        boost::filesystem::path mount_dir = TestConfig::Get().mount_dir / "clean";
        MountPoint mountPoint(TestConfig::Get().test_device, mount_dir);
        
        boost::filesystem::remove_all(mount_dir/TestConfig::Get().test_dir);
    }
    catch(std::exception& ex)
    {
        std::cerr << "Failed to clean after tests." << std::endl;
        std::cerr << ex.what() << std::endl;
    }
}

std::string PrepareSnapName()
{
    if (boost::filesystem::exists("/dev/veeamsnap"))
    {
        set_snapshot_image_name("/dev/veeamsnap");
        return "/dev/veeamimage";
    }
    
    if (boost::filesystem::exists("/dev/blk-snap"))
        return "/dev/blk-snap-image";

    throw std::runtime_error("Unable to found snapshot module");
}

int main(int argc, char* argv[])
{
    
    
    Catch::Session sess;
    
    std::string testDir = "blk-snap-tests";
    std::string mountDir = "/tmp/blk-snap-mount";
    std::string device = "";
    
    auto cli = sess.cli() |
               Catch::clara::Opt(testDir, "test dir")["--testDir"]("testDir").optional() |
               Catch::clara::Opt(mountDir, "mount dir")["--mounDir"]("mountDir").optional() |
               Catch::clara::Opt(device, "path to device")["--device"]("device for taking snapshot").optional();
    
    sess.cli(cli);
    sess.applyCommandLine(argc, argv);
    
    if (IsDiscovery(sess))
        return sess.run();
    
    LoopDevice::Ptr ptrLoop;
    if (device.empty())
    {
        ptrLoop = LoopDevice::Create("/tmp", size_t(2) * 1024 * 1024 * 1024);
        ptrLoop->Mkfs("ext4");
        device = ptrLoop->GetDevice().string();
    }
    
    TestConfig config;
    config.test_dir = testDir;
    config.mount_dir = mountDir;
    config.test_device = device;
    config.snap_image_name = PrepareSnapName();
    TestConfig::Set(config);
    
    boost::filesystem::create_directories(mountDir);
    
    int result = sess.run();
    Clean();
    return result;
};