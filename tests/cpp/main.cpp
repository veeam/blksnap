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

void CleanTestDir()
{
    try
    {
        boost::filesystem::path mount_dir = TestConfig::Get().workDir_dir / "clean";
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

    if (boost::filesystem::exists("/dev/blksnap"))
        return "/dev/blksnap-image";

    throw std::runtime_error("Unable to found snapshot module");
}

int main(int argc, char* argv[])
{


    Catch::Session sess;

    std::string testDir = "blksnap-tests";
    std::string workDir = "/tmp/blksnap-mount";
    std::string device = "";

    auto cli = sess.cli() |
               Catch::clara::Opt(testDir, "test dir")["--testDir"]("testDir").optional() |
               Catch::clara::Opt(workDir, "work dir")["--workDir"]("workDir: for mount and snap store files").optional() |
               Catch::clara::Opt(device, "path to device")["--device"]("device for taking snapshot").optional();

    sess.cli(cli);
    sess.applyCommandLine(argc, argv);

    if (IsDiscovery(sess))
        return sess.run();

    boost::filesystem::create_directories(workDir);
    LoopDevice::Ptr ptrLoop;
    if (device.empty())
    {
        ptrLoop = LoopDevice::Create(workDir, size_t(2) * 1024 * 1024 * 1024);
        ptrLoop->Mkfs("ext4");
        device = ptrLoop->GetDevice().string();
    }

    TestConfig config;
    config.test_dir = testDir;
    config.workDir_dir = workDir;
    config.test_device = device;
    config.snap_image_name = PrepareSnapName();
    TestConfig::Set(config);


    int result = sess.run();
    CleanTestDir();
    ptrLoop.reset();
    boost::filesystem::remove_all(TestConfig::Get().workDir_dir);
    return result;
};
