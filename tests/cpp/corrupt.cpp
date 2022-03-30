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

/**
 * Fill the contents of the block device with special test data.
 */
void FillAll(const std::shared_ptr<CTestSectorGenetor> ptrGen, const std::shared_ptr<CBlockDevice>& ptrBdev)
{
    AlignedBuffer<unsigned char> portion(ptrBdev->BlockSize(), 1024 * 1024);
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
    AlignedBuffer<unsigned char> portion(ptrBdev->BlockSize(), 1024 * 1024);
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

void FillBlocks(const std::shared_ptr<CTestSectorGenetor>& ptrGen, const std::shared_ptr<CBlockDevice>& ptrBdev,
                off_t offset, size_t size)
{
    AlignedBuffer<unsigned char> portion(ptrBdev->BlockSize(), size);

    ptrGen->Generate(portion.Data(), size, offset >> SECTOR_SHIFT);
    ptrBdev->Write(portion.Data(), size, offset);
}

/**
 * Fill some random blocks with random offset
 */
void FillRandomBlocks(const std::shared_ptr<CTestSectorGenetor>& ptrGen, const std::shared_ptr<CBlockDevice>& ptrBdev,
                      const int count)
{
    off_t sizeBdev = ptrBdev->Size();
    size_t blkszSectors = ptrBdev->BlockSize() >> SECTOR_SHIFT;
    size_t totalSize = 0;
    std::stringstream ss;

    logger.Detail(std::string("write " + std::to_string(count) + " transactions"));
    for (int cnt = 0; cnt < count; cnt++)
    {
        size_t size = static_cast<size_t>((std::rand() & 0x1F) + blkszSectors) * blkszSectors * SECTOR_SIZE;

#if 0
        //ordered by default chunk size
        off_t offset = static_cast<off_t>(std::rand()) << (SECTOR_SHIFT + 9);
        if (offset > (sizeBdev - size))
            offset = (offset % (sizeBdev - size)) & ~((1 << 9) - 1);
#else
        off_t offset = static_cast<off_t>(std::rand()) * blkszSectors * SECTOR_SIZE;
        if (offset > (sizeBdev - size))
            offset = offset % (sizeBdev - size);
#endif

        ss << (offset >> SECTOR_SHIFT) << ":" << (size >> SECTOR_SHIFT) << " ";
        FillBlocks(ptrGen, ptrBdev, offset, size);
        totalSize += size;
    }
    logger.Detail(ss);

    logger.Info("wrote " + std::to_string(count) + " transactions in total with "
                + std::to_string(totalSize >> SECTOR_SHIFT) + " sectors");
}

struct SCorruptInfo
{
    std::string original;
    std::vector<SRange> ranges;

    SCorruptInfo(){};
    SCorruptInfo(std::string inOriginal, const std::vector<SRange>& inRanges)
        : original(inOriginal)
        , ranges(inRanges){};
};

/*
 * @blockSize should be a power of two.
 */
static inline size_t sectorToBlock(sector_t sector, unsigned int blockSize)
{
    blockSize >>= SECTOR_SHIFT;
    return static_cast<size_t>(sector / blockSize);
}

bool IsChangedRegion(const std::shared_ptr<blksnap::SCbtInfo>& ptrCbtInfoPrevious,
                     const std::shared_ptr<blksnap::SCbtInfo>& ptrCbtInfoCurrent,
                     const std::shared_ptr<blksnap::SCbtData>& ptrCbtMap, const SRange& range)
{
    logger.Info("Is region " + std::to_string(range.sector) + ":" + std::to_string(range.count) + " has been changed ");
    if (uuid_compare(ptrCbtInfoPrevious->generationId, ptrCbtInfoCurrent->generationId))
    {
        logger.Info("Next generation found");
        return true;
    }

    if (ptrCbtInfoPrevious->snapNumber > ptrCbtInfoCurrent->snapNumber)
        throw std::runtime_error("Apparently, mixed the current and previous CBT info.");

    if (ptrCbtInfoPrevious->blockSize != ptrCbtInfoCurrent->blockSize)
        throw std::runtime_error("The CBT block size cannot be changed in one generation.");

    unsigned int blockSize = ptrCbtInfoCurrent->blockSize;
    size_t from = sectorToBlock(range.sector, blockSize);
    size_t to = sectorToBlock(range.sector + range.count - 1, blockSize);
    bool changed = false;
    logger.Info("Blocks from " + std::to_string(from) + " to " + std::to_string(to));
    for (size_t inx = from; inx <= to; inx++)
    {
        if (ptrCbtMap->vec[inx] > ptrCbtInfoPrevious->snapNumber)
        {
            changed = true;
            logger.Info("The block " + std::to_string(inx) + " has been changed");
        }
        else
        {
            logger.Info("The block " + std::to_string(inx) + " has NOT been changed");
        }
    }

    return changed;
}

void CheckCbtCorrupt(const std::shared_ptr<blksnap::SCbtInfo>& ptrCbtInfoPrevious,
                     const std::vector<SCorruptInfo>& corrupts)
{
    if (corrupts.empty())
        throw std::runtime_error("Failed to check CBT corruption. Corrupts list is empty");
    const std::string& original = corrupts[0].original;

    auto ptrCbt = blksnap::ICbt::Create();

    std::shared_ptr<blksnap::SCbtInfo> ptrCbtInfoCurrent = ptrCbt->GetCbtInfo(original);
    std::shared_ptr<blksnap::SCbtData> ptrCbtDataCurrent = ptrCbt->GetCbtData(ptrCbtInfoCurrent);

    logger.Info("Previous CBT snap number= " + std::to_string(ptrCbtInfoPrevious->snapNumber));
    logger.Info("Current CBT snap number= " + std::to_string(ptrCbtInfoCurrent->snapNumber));

    for (const SCorruptInfo& corruptInfo : corrupts)
    {
        for (const SRange& range : corruptInfo.ranges)
        {
            IsChangedRegion(ptrCbtInfoPrevious, ptrCbtInfoCurrent, ptrCbtDataCurrent, range);
        }
    }
}

void LogCurruptedSectors(const std::string& image, const std::vector<SRange>& ranges)
{
    std::stringstream ss;

    ss << ranges.size() << " corrupted ranges" << std::endl;
    ss << "Ranges of corrupted sectors:" << std::endl;
    for (const SRange& range : ranges)
    {
        blksnap::SectorState state = {0};

        ss << range.sector << ":" << range.count << std::endl;
        blksnap::GetSectorState(image, range.sector << SECTOR_SHIFT, state);
        ss << "prev= " + std::to_string(state.snapNumberPrevious) + " "
           << "curr= " + std::to_string(state.snapNumberCurrent) + " "
           << "state= " + std::to_string(state.chunkState) << std::endl;
    }
    logger.Err(ss);
}

void CheckCorruption(const std::string& origDevName, const std::string& diffStorage, const int durationLimitSec,
                     const bool isSync, const int blocksCountMax)
{
    std::vector<SCorruptInfo> corrupts;
    std::shared_ptr<blksnap::SCbtInfo> ptrCbtInfoPrevious;

    logger.Info("--- Test: check corruption ---");
    logger.Info("version: " + blksnap::Version());
    logger.Info("device: " + origDevName);
    logger.Info("diffStorage: " + diffStorage);
    logger.Info("duration: " + std::to_string(durationLimitSec) + " seconds");

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

        { // get CBT information
            char generationIdStr[64];
            auto ptrCbt = blksnap::ICbt::Create();
            auto ptrCbtInfo = ptrCbt->GetCbtInfo(origDevName);

            if (ptrCbtInfoPrevious)
            {
                if (uuid_compare(ptrCbtInfoPrevious->generationId, ptrCbtInfo->generationId))
                {
                    uuid_unparse(ptrCbtInfo->generationId, generationIdStr);
                    logger.Info("- New CBT generation [" + std::string(generationIdStr) + "] has been created.");
                }
            }
            else
            {
                uuid_unparse(ptrCbtInfo->generationId, generationIdStr);
                logger.Info("- Start with CBT generation [" + std::string(generationIdStr) + "]");
            }

            ptrCbtInfoPrevious = ptrCbtInfo;
        }

        int testSeqNumber = ptrGen->GetSequenceNumber();
        clock_t testSeqTime = std::clock();
        logger.Info("test sequence time " + std::to_string(testSeqTime));

        std::string imageDevName = ptrSession->GetImageDevice(origDevName);
        logger.Info("Found image block device [" + imageDevName + "]");
        auto ptrImage = std::make_shared<CBlockDevice>(imageDevName);

        logger.Info("- Check image content before writing to original device");

        CheckAll(ptrGen, ptrImage, testSeqNumber, testSeqTime);
        if (ptrGen->Fails() > 0)
        {
            isErrorFound = true;
            const std::vector<SRange>& ranges = ptrGen->GetFails();
            corrupts.emplace_back(origDevName, ranges);

            LogCurruptedSectors(ptrImage->Name(), ranges);
        }

        // Write first sector, like superblock
        FillBlocks(ptrGen, ptrOrininal, 0, 4096);
        // second chunk
        FillBlocks(ptrGen, ptrOrininal, 1ULL << (SECTOR_SHIFT + 9), 4096);
        // next chunk
        FillBlocks(ptrGen, ptrOrininal, 2ULL << (SECTOR_SHIFT + 9), 4096);

        std::time_t startFillRandom = std::time(nullptr);
        do
        {
            logger.Info("- Fill some random blocks");
            ptrGen->IncSequence();
            FillRandomBlocks(ptrGen, ptrOrininal, std::rand() / static_cast<int>((RAND_MAX + 1ull) / blocksCountMax));

            // Rewrite first sector again
            FillBlocks(ptrGen, ptrOrininal, 0, 4096);

            logger.Info("- Check image corruption");

            CheckAll(ptrGen, ptrImage, testSeqNumber, testSeqTime);
            if (ptrGen->Fails() > 0)
            {
                isErrorFound = true;

                const std::vector<SRange>& ranges = ptrGen->GetFails();
                corrupts.emplace_back(origDevName, ranges);

                LogCurruptedSectors(ptrImage->Name(), ranges);
            }

        } while (((std::time(nullptr) - startFillRandom) < 30) && !isErrorFound);

        std::string errorMessage;
        while (ptrSession->GetError(errorMessage))
        {
            isErrorFound = true;
            logger.Err(errorMessage);
        }

        logger.Info("-- Destroy blksnap session");
        ptrSession.reset();
    }
#if 0
    // Allow to show state of all blocks
    std::vector<SRange> testRanges;
    testRanges.emplace_back(0, ptrOrininal->Size() >> SECTOR_SHIFT);
    corrupts.emplace_back(origDevName, testRanges);
#endif

    if (!corrupts.empty())
    {
        // Create snapshot and check corrupted ranges and cbt table content.
        logger.Info("-- Create snapshot at " + std::to_string(std::clock()) + " by CPU clock");
        auto ptrSession = blksnap::ISession::Create(devices, diffStorage);

        CheckCbtCorrupt(ptrCbtInfoPrevious, corrupts);

        logger.Info("-- Destroy blksnap session");
        ptrSession.reset();
    }

    if (isErrorFound)
        throw std::runtime_error("--- Failed: singlethread check corruption ---");

    logger.Info("--- Success: check corruption ---");
}

struct SGeneratorContext
{
    std::atomic_bool stop;
    std::mutex lock;
    std::list<std::string> errorMessages;
    std::shared_ptr<CBlockDevice> ptrBdev;
    std::shared_ptr<CTestSectorGenetor> ptrGen;

    SGeneratorContext(const std::shared_ptr<CBlockDevice>& in_ptrBdev,
                      const std::shared_ptr<CTestSectorGenetor>& in_ptrGen)
        : stop(false)
        , ptrBdev(in_ptrBdev)
        , ptrGen(in_ptrGen){};
};

void GeneratorThreadFunction(std::shared_ptr<SGeneratorContext> ptrCtx)
{
    try
    {
        logger.Info("- Start writing to device [" + ptrCtx->ptrBdev->Name() + "].");
        while (!ptrCtx->stop)
        {
            FillRandomBlocks(ptrCtx->ptrGen, ptrCtx->ptrBdev, std::rand() & 0x3F);
            std::this_thread::sleep_for(std::chrono::milliseconds(std::rand() & 0x1FF));
        }
        logger.Info("- Stop writing to device [" + ptrCtx->ptrBdev->Name() + "].");
    }
    catch (std::exception& ex)
    {
        logger.Err(ex.what());

        std::lock_guard<std::mutex> guard(ptrCtx->lock);
        ptrCtx->errorMessages.emplace_back(ex.what());
    }
};

struct SCheckerContext
{
    std::atomic<bool> processed;
    std::atomic<bool> complete;
    std::mutex lock;
    std::list<std::string> errorMessages;
    std::shared_ptr<CBlockDevice> ptrBdev;
    std::shared_ptr<CTestSectorGenetor> ptrGen;
    int testSeqNumber;
    clock_t testSeqTime;

    SCheckerContext(const std::shared_ptr<CBlockDevice>& in_ptrBdev,
                    const std::shared_ptr<CTestSectorGenetor>& in_ptrGen)
        : processed(false)
        , complete(false)
        , ptrBdev(in_ptrBdev)
        , ptrGen(in_ptrGen)
    {
        testSeqNumber = ptrGen->GetSequenceNumber();
        testSeqTime = std::clock();
        ptrGen->IncSequence();
    };
};

void CheckerThreadFunction(std::shared_ptr<SCheckerContext> ptrCtx)
{
    try
    {
        logger.Info("- Start checking image device [" + ptrCtx->ptrBdev->Name() + "].");
        CheckAll(ptrCtx->ptrGen, ptrCtx->ptrBdev, ptrCtx->testSeqNumber, ptrCtx->testSeqTime);
        logger.Info("- Complete checking image device [" + ptrCtx->ptrBdev->Name() + "].");

        if (ptrCtx->ptrGen->Fails())
        {
            LogCurruptedSectors(ptrCtx->ptrBdev->Name(), ptrCtx->ptrGen->GetFails());

            throw std::runtime_error(std::to_string(ptrCtx->ptrGen->Fails()) + " corrupted sectors were found");
        }
    }
    catch (std::exception& ex)
    {
        logger.Err(ex.what());

        std::lock_guard<std::mutex> guard(ptrCtx->lock);
        ptrCtx->errorMessages.emplace_back(ex.what());
    }
    ptrCtx->complete = true;
};

void CheckCbtCorrupt(const std::map<std::string, std::shared_ptr<blksnap::SCbtInfo>>& previousCbtInfoMap,
                     const std::vector<SCorruptInfo>& corrupts)
{
    auto ptrCbt = blksnap::ICbt::Create();

    std::map<std::string, std::shared_ptr<blksnap::SCbtInfo>> currentCbtInfoMap;
    std::map<std::string, std::shared_ptr<blksnap::SCbtData>> currentCbtDataMap;
    for (const SCorruptInfo& corruptInfo : corrupts)
    {
        if (currentCbtInfoMap.count(corruptInfo.original) == 0)
        {
            auto ptrCbtInfo = ptrCbt->GetCbtInfo(corruptInfo.original);

            currentCbtInfoMap[corruptInfo.original] = ptrCbtInfo;
            currentCbtDataMap[corruptInfo.original] = ptrCbt->GetCbtData(ptrCbtInfo);
            ;
        }
    }

    for (const SCorruptInfo& corruptInfo : corrupts)
    {
        for (const SRange& range : corruptInfo.ranges)
        {
            const std::string& device = corruptInfo.original;

            IsChangedRegion(previousCbtInfoMap.at(device), currentCbtInfoMap.at(device), currentCbtDataMap.at(device),
                            range);
        }
    }
}

void MultithreadCheckCorruption(const std::vector<std::string>& origDevNames, const std::string& diffStorage,
                                const int durationLimitSec)
{
    std::map<std::string, std::shared_ptr<CTestSectorGenetor>> genMap;
    std::vector<std::thread> genThreads;
    std::vector<std::shared_ptr<SGeneratorContext>> genCtxs;
    std::vector<SCorruptInfo> corrupts;
    std::map<std::string, std::shared_ptr<blksnap::SCbtInfo>> previousCbtInfoMap;

    logger.Info("--- Test: multithread check corruption ---");
    logger.Info("version: " + blksnap::Version());
    {
        std::string mess("devices:");
        for (const std::string& origDevName : origDevNames)
            mess += std::string(" " + origDevName);
        logger.Info(mess);
    }
    logger.Info("diffStorage: " + diffStorage);
    logger.Info("duration: " + std::to_string(durationLimitSec) + " seconds");

    for (const std::string& origDevName : origDevNames)
        genMap[origDevName] = std::make_shared<CTestSectorGenetor>(false);

    for (const std::string& origDevName : origDevNames)
        genCtxs.push_back(
          std::make_shared<SGeneratorContext>(std::make_shared<CBlockDevice>(origDevName, true), genMap[origDevName]));

    // Initiate block device content for each original device
    logger.Info("-- Fill original device collection by test pattern");
    for (const std::shared_ptr<SGeneratorContext>& ptrCtx : genCtxs)
        FillAll(ptrCtx->ptrGen, ptrCtx->ptrBdev);

    // start generators for all original device
    logger.Info("-- Start generation random content to original devices ");
    for (const std::shared_ptr<SGeneratorContext>& ptrCtx : genCtxs)
        genThreads.emplace_back(GeneratorThreadFunction, ptrCtx);

    std::time_t startTime = std::time(nullptr);
    int elapsed;
    bool isErrorFound = false;
    while (((elapsed = (std::time(nullptr) - startTime)) < durationLimitSec) && !isErrorFound)
    {
        logger.Info("-- Elapsed time: " + std::to_string(elapsed) + " seconds");

        logger.Info("-- Create snapshot at " + std::to_string(std::clock()) + " by CPU clock");
        auto ptrSession = blksnap::ISession::Create(origDevNames, diffStorage);

        { // get CBT information
            char generationIdStr[64];
            auto ptrCbt = blksnap::ICbt::Create();

            previousCbtInfoMap.clear();
            for (const std::string& origDevName : origDevNames)
            {
                auto ptrCbtInfo = ptrCbt->GetCbtInfo(origDevName);

                if (!(previousCbtInfoMap.find(origDevName) == previousCbtInfoMap.end()))
                {
                    if (uuid_compare(previousCbtInfoMap.at(origDevName)->generationId, ptrCbtInfo->generationId))
                    {
                        uuid_unparse(ptrCbtInfo->generationId, generationIdStr);
                        logger.Info("- New CBT generation " + std::string(generationIdStr)
                                    + " has been created for device [" + origDevName + "]");
                    }
                }
                else
                {
                    uuid_unparse(ptrCbtInfo->generationId, generationIdStr);
                    logger.Info("- Start with CBT generation " + std::string(generationIdStr) + " for device ["
                                + origDevName + "]");
                }

                previousCbtInfoMap[origDevName] = ptrCbtInfo;
            }
        }

        // Create checker contexts
        std::vector<std::shared_ptr<SCheckerContext>> checkerCtxs;
        for (const std::string& origDevName : origDevNames)
        {
            auto ptrCtx = std::make_shared<SCheckerContext>(
              std::make_shared<CBlockDevice>(ptrSession->GetImageDevice(origDevName)), genMap[origDevName]);

            checkerCtxs.push_back(ptrCtx);
        }

#if 1
        if (elapsed > 30)
        {
            /* To check the verification algorithm, we explicitly write data to the snapshot image.*/
            logger.Info("DEBUG! write some sectors to snapshot images");
            for (const std::shared_ptr<SCheckerContext>& ptrCtx : checkerCtxs)
            {
                AlignedBuffer<char> buf(ptrCtx->ptrBdev->BlockSize());
                memset(buf.Data(), 0, buf.Size());
                strncpy(buf.Data(),
                        "To check the verification algorithm, we explicitly write data to the snapshot image.",
                        buf.Size()-1);

                std::lock_guard<std::mutex> guard(ptrCtx->lock);
                ptrCtx->ptrBdev->Write(buf.Data(), buf.Size(), 0);
                ptrCtx->ptrBdev->Write(buf.Data(), buf.Size(), (ptrCtx->ptrBdev->Size() / 2) & ~(SECTOR_SIZE - 1));
                ptrCtx->ptrBdev->Write(buf.Data(), buf.Size(), ptrCtx->ptrBdev->Size() - SECTOR_SIZE);
            }
            logger.Info("DEBUG! writing complete");
        }
#endif

        // Start checker threads
        std::vector<std::thread> checkThreads;
        for (const std::shared_ptr<SCheckerContext>& ptrCtx : checkerCtxs)
        {
            checkThreads.emplace_back(CheckerThreadFunction, ptrCtx);
            logger.Info("Checker thread is started for a block device [" + ptrCtx->ptrBdev->Name() + "]");
        }

        // Waiting for all check threads completed
        logger.Info("-- Waiting for all check threads completed");
        int completeCounter = checkerCtxs.size();
        while (completeCounter)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            logger.Info("-- Still waiting ...");
            for (const std::shared_ptr<SCheckerContext>& ptrCtx : checkerCtxs)
            {
                if (ptrCtx->complete && !ptrCtx->processed)
                {
                    if (ptrCtx->errorMessages.size() == 0)
                        logger.Info("- Successfully checking complete for device [" + ptrCtx->ptrBdev->Name() + "].");
                    else
                    {
                        isErrorFound = true;
                        std::stringstream ss;

                        ss << "- Checking for device [" << ptrCtx->ptrBdev->Name() << "] failed." << std::endl;
                        while (ptrCtx->errorMessages.size())
                        {
                            ss << ptrCtx->errorMessages.front() << std::endl;
                            ptrCtx->errorMessages.pop_front();
                        }
                        logger.Err(ss);
                        corrupts.emplace_back(ptrSession->GetOriginalDevice(ptrCtx->ptrBdev->Name()),
                                              ptrCtx->ptrGen->GetFails());
                    }
                    ptrCtx->processed = true;
                    completeCounter--;
                }
            }
        }

        for (auto& checkThread : checkThreads)
            checkThread.join();

        logger.Info("-- Check errors");

        // Check errors from blksnap session
        std::string errorMessage;
        while (ptrSession->GetError(errorMessage))
        {
            isErrorFound = true;
            logger.Err(errorMessage);
        }

        // Check errors from generator threads
        for (const std::shared_ptr<SGeneratorContext>& ptrCtx : genCtxs)
        {
            std::lock_guard<std::mutex> guard(ptrCtx->lock);

            while (ptrCtx->errorMessages.size())
            {
                isErrorFound = true;

                logger.Err(ptrCtx->errorMessages.front());
                ptrCtx->errorMessages.pop_front();
            }
        }

        // clear resources
        checkThreads.clear();
        checkerCtxs.clear();
        logger.Info("--Destroy blksnap session");
        ptrSession.reset();
    }

    // stop generators
    logger.Info("-- Stop generation random content to original devices");
    for (const std::shared_ptr<SGeneratorContext>& ptrCtx : genCtxs)
        ptrCtx->stop = true;

    for (auto& genThread : genThreads)
        genThread.join();

    if (!corrupts.empty())
    {
        // Create snapshot and check corrupted ranges and cbt table content.
        logger.Info("-- Create snapshot at " + std::to_string(std::clock()) + " by CPU clock");
        auto ptrSession = blksnap::ISession::Create(origDevNames, diffStorage);

        CheckCbtCorrupt(previousCbtInfoMap, corrupts);

        logger.Info("--Destroy blksnap session");
        ptrSession.reset();
    }

    if (isErrorFound)
        throw std::runtime_error("--- Failed: multithread check corruption ---");

    logger.Info("--- Success: multithread check corruption ---");
}

void Main(int argc, char* argv[])
{
    po::options_description desc;
    std::string usage = std::string("Checking the correctness of the COW algorithm of the blksnap module.");

    desc.add_options()
        ("help,h", "Show usage information.")
        ("log,l", po::value<std::string>(),"Detailed log of all transactions.")
        ("device,d", po::value<std::vector<std::string>>()->multitoken(),
            "Device name. It's multitoken for multithread test mod.")
        ("diff_storage,s", po::value<std::string>(),
            "Directory name for allocating diff storage files.")
        ("multithread",
            "Testing mode in which writings to the original devices and their checks are performed in parallel.")
        ("duration,u", po::value<int>(), "The test duration limit in minutes.")
        ("sync", "Use O_SYNC for access to original device.")
        ("blocks", po::value<int>(), "The maximum limit of writting blocks.")
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
    std::vector<std::string> origDevNames = vm["device"].as<std::vector<std::string>>();

    if (!vm.count("diff_storage"))
        throw std::invalid_argument("Argument 'diff_storage' is missed.");
    std::string diffStorage = vm["diff_storage"].as<std::string>();

    int duration = 5;
    if (vm.count("duration"))
        duration = vm["duration"].as<int>();

    bool isSync = false;
    if (vm.count("sync"))
        isSync = true;

    int blocksCountMax = 0x1000;
    if (vm.count("blocks"))
        blocksCountMax = vm["blocks"].as<int>();

    std::srand(std::time(0));
    if (!!vm.count("multithread"))
        MultithreadCheckCorruption(origDevNames, diffStorage, duration * 60);
    else
    {
        if (origDevNames.size() > 1)
            logger.Err("In singlethread test mode used only first device.");

        CheckCorruption(origDevNames[0], diffStorage, duration * 60,
                        isSync, blocksCountMax);
    }
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
