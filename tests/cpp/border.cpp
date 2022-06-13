// SPDX-License-Identifier: GPL-2.0+
#include <algorithm>
//#include <blksnap/Cbt.h>
//#include <blksnap/Service.h>
//#include <blksnap/Session.h>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <thread>
#include <unistd.h>

#include "helpers/AlignedBuffer.hpp"
#include "helpers/BlockDevice.h"
#include "helpers/Log.h"
#include "TestSector.h"

namespace po = boost::program_options;
namespace fs = boost::filesystem;
using blksnap::sector_t;
using blksnap::SRange;

int g_blksz = 512;

void FillBlocks(const std::shared_ptr<CTestSectorGenetor>& ptrGen,
                const std::shared_ptr<CBlockDevice>& ptrBdev,
                off_t offset, size_t size)
{
    AlignedBuffer<unsigned char> portion(g_blksz, size);

    ptrGen->Generate(portion.Data(), size, offset >> SECTOR_SHIFT);
    ptrBdev->Write(portion.Data(), size, offset);
}

void CheckBlocks(const std::shared_ptr<CTestSectorGenetor>& ptrGen,
                 const std::shared_ptr<CBlockDevice>& ptrBdev,
                 off_t offset, size_t size,
                 const int seqNumber, const clock_t seqTime)
{
    AlignedBuffer<unsigned char> portion(g_blksz, size);

    ptrBdev->Read(portion.Data(), size, offset >> SECTOR_SHIFT);

    ptrGen->Check(portion.Data(), size, offset >> SECTOR_SHIFT, seqNumber, seqTime);
}

/**
 * Fill the contents of the block device with special test data.
 */
void FillAll(const std::shared_ptr<CTestSectorGenetor> ptrGen, const std::shared_ptr<CBlockDevice>& ptrBdev)
{
    AlignedBuffer<unsigned char> portion(g_blksz, 1024 * 1024);
    off_t sizeBdev = ptrBdev->Size();
    sector_t sector = 0;

    logger.Info("device [" + ptrBdev->Name() + "] size " + std::to_string(ptrBdev->Size()) + " bytes");
    for (off_t offset = 0; offset < sizeBdev; offset += portion.Size())
    {
        size_t portionSize = std::min(portion.Size(), static_cast<size_t>(sizeBdev - offset));

        ptrGen->Generate(portion.Data(), portionSize, sector);

        ptrBdev->Write(portion.Data(), portionSize, offset);

        sector += (portionSize >> SECTOR_SHIFT);
    }
}

/**
 * Fill the contents of the block device with special test data.
 */
void CheckAll(const std::shared_ptr<CTestSectorGenetor> ptrGen, const std::shared_ptr<CBlockDevice>& ptrBdev,
              const int seqNumber, const clock_t seqTime)
{
    AlignedBuffer<unsigned char> portion(g_blksz, 1024 * 1024);
    off_t sizeBdev = ptrBdev->Size();
    sector_t sector = 0;

    logger.Info("Check on image [" + ptrBdev->Name() + "]");

    ::sync();

    for (off_t offset = 0; offset < sizeBdev; offset += portion.Size())
    {
        size_t portionSize = std::min(portion.Size(), static_cast<size_t>(sizeBdev - offset));

        ptrBdev->Read(portion.Data(), portionSize, offset);

        ptrGen->Check(portion.Data(), portionSize, sector, seqNumber, seqTime);

        sector += (portionSize >> SECTOR_SHIFT);
    }
}

void CheckBorder(const std::string& origDevName, const std::string& diffStorage, const int durationLimitSec,
                 const bool isSync, const int chunkSize)
{
    logger.Info("--- Test: boundary conditions ---");
    logger.Info("version: " + blksnap::Version());
    logger.Info("device: " + origDevName);
    logger.Info("diffStorage: " + diffStorage);
    logger.Info("duration: " + std::to_string(durationLimitSec) + " seconds");
    logger.Info("chunkSize: " + std::to_string(chunkSize) + " bytes");

    auto ptrGen = std::make_shared<CTestSectorGenetor>(false);
    auto ptrOrininal = std::make_shared<CBlockDevice>(origDevName, isSync);

    logger.Info("device size: " + std::to_string(ptrOrininal->Size()));
    logger.Info("device block size: " + std::to_string(ptrOrininal->BlockSize()));

    logger.Info("-- Fill original device collection by test pattern");
    FillAll(ptrGen, ptrOrininal);

    std::vector<std::string> devices;
    devices.push_back(origDevName);

    std::time_t startTime = std::time(nullptr);
    int elapsed;
    bool isErrorFound = false;
    while (((elapsed = (std::time(nullptr) - startTime)) < durationLimitSec) && !isErrorFound)
    {
        logger.Info("-- Elapsed time: " + std::to_string(elapsed) + " seconds");
        logger.Info("-- Create snapshot");
        auto ptrSession = blksnap::ISession::Create(devices, diffStorage);

        int testSeqNumber = ptrGen->GetSequenceNumber();
        clock_t testSeqTime = std::clock();
        logger.Info("test sequence time " + std::to_string(testSeqTime));

        std::string imageDevName = ptrSession->GetImageDevice(origDevName);
        logger.Info("Found image block device [" + imageDevName + "]");
        auto ptrImage = std::make_shared<CBlockDevice>(imageDevName);

        // Write first sector of chunk
        off_t chunk_offset = (std::rand() * static_cast<off_t>(chunkSize)) % ptrOrininal->Size();
        FillBlocks(ptrGen, ptrOrininal, chunk_offset, g_blksz);

        CheckBlocks(ptrGen, ptrOrininal, chunk_offset + (std::rand() % chunkSize & ), g_blksz, testSeqNumber, testSeqNumber);

        logger.Info("-- Destroy blksnap session");
        ptrSession.reset();
    }

    if (isErrorFound)
        throw std::runtime_error("--- Failed: boundary conditions ---");

    logger.Info("--- Success: boundary conditions ---");
}

void Main(int argc, char* argv[])
{
    po::options_description desc;
    std::string usage = std::string("Checking the correctness of the COW algorithm of the blksnap module.");

    desc.add_options()
        ("help,h", "Show usage information.")
        ("log,l", po::value<std::string>(),"Detailed log of all transactions.")
        ("device,d", po::value<std::string>(),"Device name.")
        ("diff_storage,s", po::value<std::string>(),
            "Directory name for allocating diff storage files.")
        ("duration,u", po::value<int>(), "The test duration limit in minutes.")
        ("sync", "Use O_SYNC for access to original device.")
        ("chunksize", po::value<int>(), "The size of chunks buffer.")
        ;
    po::variables_map vm;
    po::parsed_options parsed = po::command_line_parser(argc, argv).options(desc).run();
    po::store(parsed, vm);
    po::notify(vm);

    if (vm.count("help"))
    {
        std::cout << usage << std::endl;
        std::cout << desc << std::endl;
        return;
    }

    if (vm.count("log"))
    {
        std::string filename = vm["log"].as<std::string>();
        logger.Open(filename);
    }

    if (!vm.count("device"))
        throw std::invalid_argument("Argument 'device' is missed.");
    std::string origDevName = vm["device"].as<std::string>();

    if (!vm.count("diff_storage"))
        throw std::invalid_argument("Argument 'diff_storage' is missed.");
    std::string diffStorage = vm["diff_storage"].as<std::string>();

    int duration = 1;
    if (vm.count("duration"))
        duration = vm["duration"].as<int>();

    bool isSync = false;
    if (vm.count("sync"))
        isSync = true;

    int chunkSize = 256*1024;
    if (vm.count("chunksize"))
        chunkSize = vm["chunksize"].as<int>();

    CheckBorder(origDevName, diffStorage, duration * 60, isSync, chunkSize);

}

int main(int argc, char* argv[])
{
#if 0
    Main(argc, argv);
#else
    try
    {
        Main(argc, argv);
    }
    catch (std::exception& ex)
    {
        logger.Err(ex.what());
        return 1;
    }
#endif
    return 0;
}
