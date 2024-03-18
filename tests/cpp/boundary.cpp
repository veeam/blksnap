// SPDX-License-Identifier: GPL-2.0+
#include <algorithm>
#include <blksnap/Cbt.h>
#include <blksnap/Service.h>
#include <blksnap/Session.h>
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
                off_t offset, size_t size, const clock_t seqTime)
{
    AlignedBuffer<unsigned char> portion(g_blksz, size);

    ptrGen->Generate(portion.Data(), size, offset >> SECTOR_SHIFT, seqTime);
    ptrBdev->Write(portion.Data(), size, offset);
}

void CheckBlocks(const std::shared_ptr<CTestSectorGenetor>& ptrGen,
                 const std::shared_ptr<CBlockDevice>& ptrBdev,
                 const off_t offset, const size_t size,
                 const int seqNumber, const clock_t seqTime,
                 const std::string& testName,
                 const std::string& failMessage,
                 const bool isStrictly=false)
{
    AlignedBuffer<unsigned char> portion(g_blksz, size);

    ptrBdev->Read(portion.Data(), size, offset);
    ptrGen->Check(portion.Data(), size, offset >> SECTOR_SHIFT, seqNumber, seqTime, isStrictly);

    if (ptrGen->Fails() > 0)
    {
        for (const SRange& rg : ptrGen->GetFails())
            logger.Info("FAIL: " + std::to_string(rg.sector) + ":" + std::to_string(rg.count));

        throw std::runtime_error("In check: "+testName+"\nFail: "+failMessage);
    }
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

static inline off_t randomChunk(const int chunkSize, const off_t downLimit, const off_t upLimit, std::map<off_t, bool>& excludeHistory)
{
    while (true)
    {
        off_t ret = downLimit + (std::rand() * static_cast<off_t>(chunkSize)) % upLimit;

        if (excludeHistory.find(ret) == excludeHistory.end())
            return ret;

        logger.Info("The chunk " + std::to_string(ret) + " has probably been changed.");
    }
}

static inline std::string GetVersion()
{
    unsigned short major, minor, revision, build;

    blksnap::CService().Version(major, minor, revision, build);
    return std::string(std::to_string(major)+"."+
                       std::to_string(minor)+"."+
                       std::to_string(revision)+"."+
                       std::to_string(build));
}

static inline int randomInt(const int upLimit, const int order)
{
    return (std::rand() % upLimit) & ~(order - 1);
}

void CheckBoundary(const std::string& origDevName, const std::string& diffStorage,
                   const unsigned long long diffStorageLimit, const int durationLimitSec,
                   const bool isSync, const int chunkSize)
{
    std::srand(std::time(nullptr));

    logger.Info("--- Test: boundary conditions ---");
    logger.Info("version: " + GetVersion());
    logger.Info("device: " + origDevName);
    logger.Info("diffStorage: " + diffStorage);
    logger.Info("diffStorageLimit: " + std::to_string(diffStorageLimit));
    logger.Info("duration: " + std::to_string(durationLimitSec) + " seconds");
    logger.Info("chunkSize: " + std::to_string(chunkSize) + " bytes");

    auto ptrGen = std::make_shared<CTestSectorGenetor>(false);
    auto ptrOrininal = std::make_shared<CBlockDevice>(origDevName, isSync);

    logger.Info("device size: " + std::to_string(ptrOrininal->Size()));
    logger.Info("block size: " + std::to_string(g_blksz));

    logger.Info("-- Fill original device collection by test pattern");
    FillAll(ptrGen, ptrOrininal);

    std::vector<std::string> devices;
    devices.push_back(origDevName);

    std::time_t startTime = std::time(nullptr);
    int elapsed;
    while (((elapsed = (std::time(nullptr) - startTime)) < durationLimitSec))
    {
        logger.Info("-- Elapsed time: " + std::to_string(elapsed) + " seconds");
        logger.Info("-- Create snapshot");

        {
            auto ptrSession = blksnap::ISession::Create(devices, diffStorage, diffStorageLimit);

            int testSeqNumber = ptrGen->GetSequenceNumber();
            ptrGen->IncSequence();
            clock_t testSeqTime = std::clock();

            logger.Info("test sequence time " + std::to_string(testSeqTime));

            std::string imageDevName = blksnap::ICbt::Create(origDevName)->GetImage();
            logger.Info("Found image block device [" + imageDevName + "]");
            auto ptrImage = std::make_shared<CBlockDevice>(imageDevName);

            std::map<off_t, bool> history;

            logger.Info("-- Сhecking the writing in the original device");
            for (int inx=0; (inx < 128); inx++)
            {
                clock_t seqTime = std::clock();
                off_t chunkOffset = randomChunk(chunkSize, chunkSize, ptrOrininal->Size() - 2*chunkSize, history);
                history[chunkOffset] = true;

                logger.Info("Check offset " + std::to_string(chunkOffset));

                {
                    const std::string testName("Write first sector of random chunk in original device");
                    FillBlocks(ptrGen, ptrOrininal, chunkOffset, g_blksz, seqTime);

                    CheckBlocks(ptrGen, ptrOrininal, chunkOffset + g_blksz, chunkSize - g_blksz,
                                testSeqNumber, testSeqTime, testName,
                                "Failed to check other sectors in the chunk on original device");

                    CheckBlocks(ptrGen, ptrImage, chunkOffset + randomInt(chunkSize, g_blksz), g_blksz,
                                testSeqNumber, testSeqTime, testName,
                                "Failed to check random sectors of the chunk in image");

                    CheckBlocks(ptrGen, ptrImage, chunkOffset - g_blksz, g_blksz,
                                testSeqNumber, testSeqTime, testName,
                                "Failed to check the sector before written chunk");

                    CheckBlocks(ptrGen, ptrImage, chunkOffset + chunkSize, g_blksz,
                                testSeqNumber, testSeqTime, testName,
                                "Failed to check the sector after written chunk");
                }
                {
                    const std::string testName("Write last sector of chunk in original device");
                    FillBlocks(ptrGen, ptrOrininal, chunkOffset + chunkSize - g_blksz, g_blksz, seqTime);

                    CheckBlocks(ptrGen, ptrOrininal, chunkOffset + g_blksz, chunkSize - 2 * g_blksz,
                                testSeqNumber, testSeqTime, testName,
                                "Failed to check other sectors in a chunk in the original device");

                    CheckBlocks(ptrGen, ptrImage, chunkOffset + randomInt(chunkSize, g_blksz), g_blksz,
                                testSeqNumber, testSeqTime, testName,
                                "Failed to check random sectors of a chunk in the image");

                    CheckBlocks(ptrGen, ptrImage, chunkOffset - g_blksz, g_blksz,
                                testSeqNumber, testSeqTime, testName,
                                "Failed to check the sector before a chunk  in the image");

                    CheckBlocks(ptrGen, ptrImage, chunkOffset + chunkSize, g_blksz,
                                testSeqNumber, testSeqTime, testName,
                                "Failed to check the sector after chunk in image");

                    CheckBlocks(ptrGen, ptrOrininal, chunkOffset, g_blksz,
                                ptrGen->GetSequenceNumber(), seqTime, testName,
                                "Failed to check the first written sector",
                                true);

                    CheckBlocks(ptrGen, ptrOrininal, chunkOffset + chunkSize - g_blksz, g_blksz,
                                ptrGen->GetSequenceNumber(), seqTime, testName,
                                "Failed to check the last written  sector",
                                true);
                }
            }
            logger.Info("-- Destroy blksnap session");
        }

        {
            auto ptrSession = blksnap::ISession::Create(devices, diffStorage, diffStorageLimit);

            int testSeqNumber = ptrGen->GetSequenceNumber();
            clock_t testSeqTime = std::clock();
            ptrGen->IncSequence();

            logger.Info("test sequence time " + std::to_string(testSeqTime));

            std::string imageDevName = blksnap::ICbt::Create(origDevName)->GetImage();;
            logger.Info("Found image block device [" + imageDevName + "]");
            auto ptrImage = std::make_shared<CBlockDevice>(imageDevName);

            std::map<off_t, bool> history;

            logger.Info("-- Сhecking the writing in the image device");
            for (int inx=0; inx<128; inx++)
            {
                clock_t seqTime = std::clock();
                off_t chunkOffset = randomChunk(chunkSize, chunkSize, ptrImage->Size() - 4*chunkSize, history);
                history[chunkOffset] = true;
                history[chunkOffset-chunkSize] = true;
                history[chunkOffset+chunkSize] = true;
                history[chunkOffset+2*chunkSize] = true;

                logger.Info("Check offset " + std::to_string(chunkOffset));

                {
                    const std::string testName("Write first sector of chunk in image");
                    FillBlocks(ptrGen, ptrImage, chunkOffset, g_blksz, seqTime);

                    CheckBlocks(ptrGen, ptrImage, chunkOffset + g_blksz, chunkSize - g_blksz,
                                testSeqNumber, testSeqTime, testName,
                                "Failed to check other sectors in the written chunk");

                    CheckBlocks(ptrGen, ptrImage, chunkOffset - g_blksz, g_blksz,
                                testSeqNumber, testSeqTime, testName,
                                "Failed to check sector before the written chunk");

                    CheckBlocks(ptrGen, ptrImage, chunkOffset + chunkSize, g_blksz,
                                testSeqNumber, testSeqTime, testName,
                                "Failed to check sector after the written chunk");
                }
                {
                    const std::string testName("Write last sector of chunk in image");
                    FillBlocks(ptrGen, ptrImage, chunkOffset + chunkSize - g_blksz, g_blksz, seqTime);

                    CheckBlocks(ptrGen, ptrImage, chunkOffset + g_blksz, chunkSize - 2*g_blksz,
                                testSeqNumber, testSeqTime, testName,
                                "Failed to check other sectors in the chunk");

                    CheckBlocks(ptrGen, ptrImage, chunkOffset - g_blksz, g_blksz,
                                testSeqNumber, testSeqTime,  testName,
                                "Failed to check sector before the written chunk");

                    CheckBlocks(ptrGen, ptrImage, chunkOffset + chunkSize, g_blksz,
                                testSeqNumber, testSeqTime,  testName,
                                "Failed to check sector after the written chunk");

                    CheckBlocks(ptrGen, ptrImage, chunkOffset, g_blksz,
                                ptrGen->GetSequenceNumber(), seqTime,  testName,
                                "Failed to check the first written sector of chunk",
                                true);

                    CheckBlocks(ptrGen, ptrImage, chunkOffset + chunkSize - g_blksz, g_blksz,
                                ptrGen->GetSequenceNumber(), seqTime,  testName,
                                "Failed to check the last written sector of chunk",
                                true);
                }
            }
            logger.Info("-- Destroy blksnap session");
        }

        {
            auto ptrSession = blksnap::ISession::Create(devices, diffStorage, diffStorageLimit);

            int testSeqNumber = ptrGen->GetSequenceNumber();
            clock_t testSeqTime = std::clock();
            ptrGen->IncSequence();

            logger.Info("test sequence time " + std::to_string(testSeqTime));

            std::string imageDevName = blksnap::ICbt::Create(origDevName)->GetImage();;
            logger.Info("Found image block device [" + imageDevName + "]");
            auto ptrImage = std::make_shared<CBlockDevice>(imageDevName);

            std::map<off_t, bool> history;

            logger.Info("-- Сhecking the writing on the border of two chunks in the image device");
            for (int inx=0; inx<128; inx++)
            {
                clock_t seqTime = std::clock();
                off_t chunkOffset = randomChunk(chunkSize, 0, ptrImage->Size() - chunkSize, history);
                history[chunkOffset] = true;
                history[chunkOffset-chunkSize] = true;
                history[chunkOffset+chunkSize] = true;

                logger.Info("Check offset " + std::to_string(chunkOffset));

                const std::string testName("Write two sectors on the border of two chunks in image");
                FillBlocks(ptrGen, ptrImage, chunkOffset + chunkSize - g_blksz, 2*g_blksz, seqTime);

                CheckBlocks(ptrGen, ptrImage, chunkOffset, chunkSize - g_blksz,
                            testSeqNumber, testSeqTime, testName,
                            "Failed to check other sectors in the first chunk");

                CheckBlocks(ptrGen, ptrImage, chunkOffset + chunkSize + g_blksz, chunkSize - g_blksz,
                            testSeqNumber, testSeqTime, testName,
                            "Failed to check other sectors in the last chunk");

                CheckBlocks(ptrGen, ptrImage, chunkOffset + chunkSize - g_blksz, 2*g_blksz,
                            ptrGen->GetSequenceNumber(), seqTime, testName,
                            "Check written sectors",
                            true);
            }

            logger.Info("-- Destroy blksnap session");
        }
    }
}

void Main(int argc, char* argv[])
{
    po::options_description desc;
    std::string usage = std::string("Checking the boundary conditions.");

    desc.add_options()
        ("help,h", "Show usage information.")
        ("log,l", po::value<std::string>(),"Detailed log of all transactions.")
        ("device,d", po::value<std::string>(),"Device name.")
        ("diff_storage,s", po::value<std::string>(),
            "The name of the file to allocate the difference storage.")
        ("diff_storage_limit,l", po::value<std::string>()->default_value("1G"),
            "The available limit for the size of the difference storage file. The suffixes M, K and G is allowed.")
        ("duration,u", po::value<int>(), "The test duration limit in minutes.")
        ("sync", "Use O_SYNC for access to original device.")
        ("blksz", po::value<int>()->default_value(512), "Align reads and writes to the block size.")
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

    unsigned long long diffStorageLimit = 0;
    unsigned long long multiple = 1;
    std::string limit_str = vm["diff_storage_limit"].as<std::string>();
    switch (limit_str.back())
    {
        case 'G':
            multiple *= 1024;
        case 'M':
            multiple *= 1024;
        case 'K':
            multiple *= 1024;
            limit_str.back() = '\0';
        default:
            diffStorageLimit = std::stoll(limit_str.c_str()) * multiple;
    }

    int duration = 1;
    if (vm.count("duration"))
        duration = vm["duration"].as<int>();

    bool isSync = false;
    if (vm.count("sync"))
        isSync = true;

    int chunkSize = 256*1024;
    if (vm.count("chunksize"))
        chunkSize = vm["chunksize"].as<int>();

    g_blksz = vm["blksz"].as<int>();
    logger.Info("blksz: " + std::to_string(g_blksz));

    try
    {
        CheckBoundary(origDevName, diffStorage, diffStorageLimit,
                      duration * 60, isSync, chunkSize);
    }
    catch (std::exception& ex)
    {
        logger.Err(ex.what());
        throw std::runtime_error("--- Failed: boundary conditions ---");
    }
    logger.Info("--- Success: boundary conditions ---");
}

int main(int argc, char* argv[])
{
    try
    {
        Main(argc, argv);
    }
    catch (std::exception& ex)
    {
        logger.Err(ex.what());
        return 1;
    }

    return 0;
}
