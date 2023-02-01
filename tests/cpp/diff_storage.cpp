// SPDX-License-Identifier: GPL-2.0+
#include <algorithm>
#include <cstdlib>
#include <blksnap/Service.h>
#include <blksnap/Cbt.h>
#include <blksnap/Session.h>
#include <boost/program_options.hpp>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "helpers/AlignedBuffer.hpp"
#include "helpers/BlockDevice.h"
#include "helpers/Log.h"
#include "helpers/RandomHelper.h"
#include "TestSector.h"

namespace po = boost::program_options;
using blksnap::sector_t;
using blksnap::SRange;

int g_blksz = 512;

//#define PAGE_SECTORS_SHIFT  (PAGE_SHIFT - SECTOR_SHIFT)
//#define PAGE_SECTORS        (1 << PAGE_SECTORS_SHIFT)
sector_t page_sectors = 4096 / 512;

//#define SECTOR_MASK     (PAGE_SECTORS - 1)
sector_t sector_mask = (4096 / 512) - 1;


void GenerateRangeMap(std::vector<SRange>& availableRanges, std::vector<SRange>& diffStorageRanges,
    const int granularity, const sector_t deviceSize)
{
    std::vector<sector_t> clip;
    double scaling =static_cast<double>(deviceSize) / (RAND_MAX + 1ul);

    for (int inx=0; inx<granularity; inx++)
    {
        sector_t sector = static_cast<sector_t>(std::rand() * scaling) & ~sector_mask;

        if ((sector == 0) || (sector > deviceSize))
            continue;

        clip.push_back(sector);
    }
    clip.push_back(deviceSize);
    std::sort(clip.begin(), clip.end());

    sector_t prevOffset = 0;
    for (int inx=0; inx<clip.size(); inx++)
    {
        sector_t currentOffset = clip[inx];
        sector_t clipSize = currentOffset - prevOffset;

        if (clipSize <= 16)
            continue;

        int diffStoreRangeSize = (page_sectors + std::rand() / static_cast<int>((RAND_MAX + 1ull) / (clipSize >> 1))) & ~sector_mask;

        availableRanges.emplace_back(prevOffset, clipSize - diffStoreRangeSize);
        diffStorageRanges.emplace_back(currentOffset - diffStoreRangeSize, diffStoreRangeSize);

        prevOffset = currentOffset;
    }
}

static void FillRange(const std::shared_ptr<CTestSectorGenetor> ptrGen,
                      const std::shared_ptr<CBlockDevice>& ptrBdev,
                      const SRange& rg)
{
    AlignedBuffer<unsigned char> portion(g_blksz, 1024 * 1024);

    off_t from = rg.sector * SECTOR_SIZE;
    off_t to = (rg.sector + rg.count) * SECTOR_SIZE;

    for (off_t offset = from; offset < to; offset += portion.Size())
    {
        size_t portionSize = std::min(portion.Size(), static_cast<size_t>(to - offset));

        ptrGen->Generate(portion.Data(), portionSize, offset >> SECTOR_SHIFT);

        //logger.Info("Writing. offset=" + std::to_string(offset) + " size=" + std::to_string(portionSize));
        ptrBdev->Write(portion.Data(), portionSize, offset);
    }
}

static void FillArea(const std::shared_ptr<CTestSectorGenetor> ptrGen,
                     const std::shared_ptr<CBlockDevice>& ptrBdev,
                     const std::vector<SRange>& area)
{
    for (const SRange& rg : area)
        FillRange(ptrGen, ptrBdev, rg);
}

static void CheckRange(const std::shared_ptr<CTestSectorGenetor> ptrGen,
                       const std::shared_ptr<CBlockDevice>& ptrBdev,
                       const SRange& rg,
                       const int seqNumber, const clock_t seqTime)
{
    AlignedBuffer<unsigned char> portion(g_blksz, 1024 * 1024);

    off_t from = rg.sector * SECTOR_SIZE;
    off_t to = (rg.sector + rg.count) * SECTOR_SIZE;

    try
    {
        for (off_t offset = from; offset < to; offset += portion.Size())
        {
            size_t portionSize = std::min(portion.Size(), static_cast<size_t>(to - offset));

            ptrBdev->Read(portion.Data(), portionSize, offset);

            ptrGen->Check(portion.Data(), portionSize, offset >> SECTOR_SHIFT, seqNumber, seqTime);
        }
    }
    catch(std::exception& ex)
    {
        throw std::runtime_error("Check range failed: \"" + std::string(ex.what()) + "\"");
    }
}

static void CheckArea(const std::shared_ptr<CTestSectorGenetor> ptrGen,
                      const std::shared_ptr<CBlockDevice>& ptrBdev,
                      const std::vector<SRange>& area,
                      const int seqNumber, const clock_t seqTime)
{
    for (const SRange& rg : area)
        CheckRange(ptrGen, ptrBdev, rg, seqNumber, seqTime);
}

static bool FindRange(const std::vector<SRange>& area, const sector_t sector, SRange& rg)
{
    size_t leftLimit = 0;
    size_t rightLimit = area.size() - 1;
    size_t inx;

    while (leftLimit <= rightLimit)
    {
        inx = leftLimit + (rightLimit - leftLimit) / 2 ;

        if (sector < area[inx].sector)
        {
            if (rightLimit == inx)
                rightLimit--;
            else
                rightLimit = inx;
            continue;
        }
        if (sector > (area[inx].sector + area[inx].count))
        {
            if (leftLimit == inx)
                leftLimit++;
            else
                leftLimit = inx;
            continue;
        }

        rg = area[inx];
        return true;
    }

    return false;
}

static bool NormalizeRange(const std::vector<SRange>& availableRanges, SRange& rg)
{
    sector_t from = rg.sector;
    sector_t to = rg.sector + rg.count - 1;
    SRange availableRange;

    if (!FindRange(availableRanges, from, availableRange) && !FindRange(availableRanges, to, availableRange))
    {
        //logger.Info("Rande " + std::to_string(rg.sector) + ":" + std::to_string(rg.count) + " does not fall into the allowed area");
        return false;
    }

    if (from < availableRange.sector)
        from = availableRange.sector;
    if (to > (availableRange.sector + availableRange.count - 1))
        to = availableRange.sector + availableRange.count - 1;

    rg.sector = from;
    rg.count = to - from + 1;
    return true;
}

static void GenerateRandomRanges(std::shared_ptr<CBlockDevice> ptrOrininal,
                                 const std::vector<SRange>& availableRanges,
                                 std::vector<SRange>& writeRanges,
                                 const int granularity, const int blockSizeLimit)
{
    sector_t deviceSize = ptrOrininal->Size() >> SECTOR_SHIFT;
    double offsetScaling = static_cast<double>(deviceSize) / (RAND_MAX + 1ul);
    int blockSizeScaling = static_cast<unsigned int>((RAND_MAX + 1ul) / (blockSizeLimit - (g_blksz >> SECTOR_SHIFT)));
    unsigned long long blockSizeMask = ~(static_cast<unsigned long long>((g_blksz >> SECTOR_SHIFT)) - 1ull);

    logger.Info("Write block list generating with granularity=" + std::to_string(granularity) +
                " and blockSizeLimit=" + std::to_string(blockSizeLimit));

    while (writeRanges.size() < granularity)
    {
        SRange rg;

        rg.sector = static_cast<sector_t>(std::rand() * offsetScaling) & blockSizeMask;
        rg.count = (page_sectors + std::rand() / blockSizeScaling) & blockSizeMask;

        if (!NormalizeRange(availableRanges, rg))
            continue;

        writeRanges.push_back(rg);
    }
}

static void LogRanges(const std::string& header, const std::vector<SRange>& ranges)
{
    sector_t totalSectors = 0;

    logger.Info(header);
    for (const SRange& rg : ranges)
    {
        logger.Info(std::to_string(rg.sector) + " - " + std::to_string(rg.sector + rg.count - 1));
        totalSectors += rg.count;
    }
    logger.Info("Total sectors: " + std::to_string(totalSectors));
}

static void CheckDiffStorage(const std::string& origDevName, const int durationLimitSec, const bool isSync)
{
    std::vector<SRange> diffStorage;

    logger.Info("--- Test: diff storage ---");
    logger.Info("version: " + blksnap::Version());
    logger.Info("device: " + origDevName);
    logger.Info("duration: " + std::to_string(durationLimitSec) + " seconds");

    auto ptrGen = std::make_shared<CTestSectorGenetor>(false);
    auto ptrOrininal = std::make_shared<CBlockDevice>(origDevName, isSync/*, 1024*1024*1024ull*/);

    logger.Info("device size: " + std::to_string(ptrOrininal->Size()));
    logger.Info("device block size: " + std::to_string(g_blksz));

    std::vector<std::string> devices;
    devices.push_back(origDevName);

    std::time_t startTime = std::time(nullptr);
    int elapsed;
    bool isErrorFound = false;

    {
        logger.Info("Fill all device by test pattern");
        std::vector<SRange> area;
        area.emplace_back(0, ptrOrininal->Size() >> SECTOR_SHIFT);
        FillArea(ptrGen, ptrOrininal, area);

        int testSeqNumber = ptrGen->GetSequenceNumber();
        clock_t testSeqTime = std::clock();
        logger.Info("test sequence time " + std::to_string(testSeqTime));

        logger.Info("Check all device using test pattern");
        CheckArea(ptrGen, ptrOrininal, area, testSeqNumber, testSeqTime);
        if (ptrGen->Fails() > 0)
        {
            isErrorFound = true;

            const std::vector<SRange>& ranges = ptrGen->GetFails();
            for (const SRange& rg : ranges)
            {
                logger.Info("FAIL: " + std::to_string(rg.sector) + ":" + std::to_string(rg.count));
            }
        }

        /*{
            AlignedBuffer<char> buf(g_blksz);

            logger.Info("Original first sector:");
            ptrOrininal->Read(buf.Data(), buf.Size(), 0 << SECTOR_SHIFT);
            logger.Err(buf.Data(), 128);
        }*/
    }
    while (((elapsed = (std::time(nullptr) - startTime)) < durationLimitSec) && !isErrorFound)
    {
        logger.Info("-- Elapsed time: " + std::to_string(elapsed) + " seconds");

        std::vector<SRange> availableRanges;
        blksnap::SStorageRanges diffStorageRanges;
        diffStorageRanges.device = ptrOrininal->Name();

        GenerateRangeMap(availableRanges, diffStorageRanges.ranges, 20, ptrOrininal->Size() >> SECTOR_SHIFT);
        LogRanges("availableRanges:", availableRanges);
        LogRanges("diffStorageRanges:", diffStorageRanges.ranges);

        logger.Info("-- Create snapshot");

        auto ptrSession = blksnap::ISession::Create(devices, diffStorageRanges);

        int testSeqNumber = ptrGen->GetSequenceNumber();
        clock_t testSeqTime = std::clock();
        logger.Info("test sequence time " + std::to_string(testSeqTime));

        std::string imageDevName = blksnap::ICbt::Create(origDevName)->GetImage();
        logger.Info("Found image block device [" + imageDevName + "]");
        auto ptrImage = std::make_shared<CBlockDevice>(imageDevName);

        /*{
            AlignedBuffer<char> buf(g_blksz);

            logger.Info("Images first sector:");
            ptrImage->Read(buf.Data(), buf.Size(), 0 << SECTOR_SHIFT);
            logger.Info(buf.Data(), 128);
        }*/

        std::vector<SRange> writeRanges;
        GenerateRandomRanges(ptrOrininal, availableRanges, writeRanges, 100, 512);
        {
            int totalCount = 0;
            for (const SRange& rg : writeRanges)
            {
                //logger.Info(std::to_string(rg.sector) + ":" + std::to_string(rg.count));
                totalCount += rg.count;
            }

            logger.Info("Generated " + std::to_string(writeRanges.size()) + " write blocks with " + std::to_string(totalCount) + " sectors.");
        }

        FillArea(ptrGen, ptrOrininal, writeRanges);
        logger.Info("Test data has been written.");

        CheckArea(ptrGen, ptrImage, availableRanges, testSeqNumber, testSeqTime);
        if (ptrGen->Fails() > 0)
        {
            isErrorFound = true;

            const std::vector<SRange>& ranges = ptrGen->GetFails();
            for (const SRange& rg : ranges)
            {
                logger.Info("FAIL: " + std::to_string(rg.sector) + " - " + std::to_string(rg.sector + rg.count - 1));

                AlignedBuffer<char> buf(g_blksz);
                ptrOrininal->Read(buf.Data(), buf.Size(), rg.sector << SECTOR_SHIFT);
                logger.Err(buf.Data(), 128);
            }
        }
        else
            logger.Info("No corrupt to the snapshot image was detected.");

        logger.Info("-- Destroy blksnap session");
        ptrSession.reset();

        if (!isErrorFound)
        {
            logger.Info("Cleanup diff storage ranges");
            FillArea(ptrGen, ptrOrininal, diffStorageRanges.ranges);

            ptrGen->IncSequence();
        }
    }
    if (isErrorFound)
        throw std::runtime_error("--- Failed: singlethread diff storage ---");

    logger.Info("--- Success: diff storage ---");
}

void Main(int argc, char* argv[])
{
    po::options_description desc;
    std::string usage = std::string("Checking the correctness of the COW algorithm of the blksnap module.");

    desc.add_options()
        ("help,h", "Show usage information.")
        ("log,l", po::value<std::string>()->default_value("/var/log/blksnap_diff_storage.log"), "Detailed log of all transactions.")
        ("device,d", po::value<std::string>(), "Device name. ")
        ("duration,u", po::value<int>()->default_value(5), "The test duration limit in minutes.")
        ("sync", "Use O_SYNC for access to original device.")
        ("blksz", po::value<int>()->default_value(512), "Align reads and writes to the block size.");
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

    logger.Info("Parameters:");

    std::string log = vm["log"].as<std::string>();
    logger.Info("log: " + log);
    logger.Open(log);

    if (!vm.count("device"))
        throw std::invalid_argument("Argument 'device' is missed.");
    std::string origDevName = vm["device"].as<std::string>();
    logger.Info("device: " + origDevName);

    int duration = vm["duration"].as<int>();
    logger.Info("duration: " + std::to_string(duration));

    bool isSync = !!(vm.count("sync"));
    logger.Info("sync: " + std::to_string(isSync));

    g_blksz = vm["blksz"].as<int>();
    logger.Info("blksz: " + std::to_string(g_blksz));

    if (g_blksz < getpagesize())
        throw std::invalid_argument("Block size cannot be less than page size '"+std::to_string(getpagesize())+"'.");

    page_sectors = getpagesize() / 512;
    sector_mask = page_sectors - 1;

    std::srand(std::time(0));
    CheckDiffStorage(origDevName, duration * 60, isSync);
}

int main(int argc, char* argv[])
{
    try
    {
        Main(argc, argv);
    }
    catch (std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
