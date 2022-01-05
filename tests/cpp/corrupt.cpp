
#include <ctime>
#include <thread>
#include <atomic>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <zlib.h>

#include "helpers/Log.h"
#include "helpers/RandomHelper.h"
#include "helpers/BlockDevice.h"
#include "helpers/AlignedBuffer.hpp"
#include "blksnap/BlksnapSession.h"

#include <boost/program_options.hpp>
namespace po = boost::program_options;
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;

#ifndef SECTOR_SHIFT
#define SECTOR_SHIFT 9
#endif
#ifndef SECTOR_SIZE
#define SECTOR_SIZE (1 << SECTOR_SHIFT)
#endif
typedef unsigned long long sector_t;

#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)

struct STestHeader
{
    int crc;
    int seqNumber;
    clock_t seqTime;
    sector_t sector;
};

struct STestSector
{
    STestHeader header;
    char body[SECTOR_SIZE-sizeof(STestHeader)];
};

struct SRange
{
    sector_t sector;
    sector_t count;

    SRange(sector_t inSector, sector_t inCount)
        : sector(inSector)
        , count(inCount)
    {};
};

enum EFailType
{
    eFailCorruptedSector,
    eFailIncorrectSector,
};

class CTestSectorGenetor
{
public:
    CTestSectorGenetor()
        : m_seqNumber(0)
        , m_failCount(0)
        , m_logLineCount(0)
        , m_isCrc32Checking(true)
    {

    };
    ~CTestSectorGenetor()
    {

    };

    void IncSequence()
    {
        m_seqNumber++;
    };

    int GetSequenceNumber()
    {
        return m_seqNumber;
    };

    void Generate(unsigned char *buffer, size_t size, sector_t sector)
    {
        for (size_t offset = 0; offset < size; offset += SECTOR_SIZE) {
            STestSector *current = (STestSector *)(buffer + offset);

            CRandomHelper::GenerateBuffer(current->body, sizeof(current->body));

            STestHeader *header = &current->header;
            header->seqNumber = m_seqNumber;
            header->seqTime = std::clock();
            header->sector = sector;
            if (m_isCrc32Checking)
                header->crc = crc32(0, buffer + offset + sizeof(header->crc), SECTOR_SIZE-sizeof(header->crc));
            else
                header->crc = 'CC32';

            sector++;
        }
    };

    void Check(unsigned char *buffer, size_t size, sector_t sector, const int seqNumber, const clock_t seqTime)
    {
        for (size_t offset = 0; offset < size; offset += SECTOR_SIZE) {
            struct STestSector *current = (STestSector *)buffer;
            STestHeader *header = &current->header;
            int crc;
            if (m_isCrc32Checking)
                crc = crc32(0, buffer+sizeof(header->crc), SECTOR_SIZE-sizeof(header->crc));
            else
                crc = 'CC32';

            bool isCorrupted = (crc != header->crc);
            bool isIncorrect = (sector != header->sector);
            bool isInvalidSeqNumber = (header->seqNumber > seqNumber);
            bool isInvalidSeqTime = (header->seqTime > seqTime);

            if (isCorrupted || isIncorrect || isInvalidSeqNumber || isInvalidSeqTime) {
                std::string failMessage;

                if (m_logLineCount == 30)
                    failMessage = "Too many sectors failed\n";
                else if (m_logLineCount < 30) {
                    if (isCorrupted) {
                        failMessage += std::string("Corrupted sector\n");
                        failMessage += std::string("sector " + std::to_string(sector) + "\n");
                        failMessage += std::string("crc " + std::to_string(header->crc) + " != " + std::to_string(crc) + "\n");
                    }
                    if (isIncorrect) {
                        failMessage += std::string("Incorrect sector\n");
                        failMessage += std::string("sector " + std::to_string(header->sector) + " != " + std::to_string(sector) + "\n");
                    }
                    if (isInvalidSeqNumber) {
                        failMessage += std::string("Invalid sequence number\n");
                        failMessage += std::string("sector " + std::to_string(header->sector) + "\n");
                        failMessage += std::string("seqNumber " + std::to_string(header->seqNumber) + " > " + std::to_string(seqNumber) + "\n");
                    }
                    if (isInvalidSeqTime) {
                        failMessage += std::string("Invalid sequence time\n");
                        failMessage += std::string("sector " + std::to_string(header->sector) + "\n");
                        failMessage += std::string("seqTime " + std::to_string(header->seqTime) + " > " + std::to_string(seqTime) + "\n");
                    }
                }

                SetFailedSector(sector, failMessage);
            }


            sector++;
            buffer += SECTOR_SIZE;
        }
    };

    int Fails()
    {
        return m_failCount;
    };
    /**
     * The function ShowFails() does not contain locks, since it should be
     * called when only one thread has access to used data.
     */
    void ShowFails()
    {
        std::stringstream ss;

        ss << m_failCount << " corrupted sectors" << std::endl;
        ss << m_failedRanges.size() << " corrupted ranges" << std::endl;
        ss << "Ranges of corrupted sectors:" << std::endl;
        for (const SRange &range : m_failedRanges)
            ss << range.sector << ":" << range.count << std::endl;
        logger.Err(ss);
    };

private:
    std::atomic<int> m_seqNumber;
    int m_failCount;
    int m_logLineCount;
    std::vector<SRange> m_failedRanges;
    bool m_isCrc32Checking;

private:
    void LogSector(sector_t sector, const std::string &failMessage)
    {
        if (failMessage.empty())
            return;

        m_logLineCount++;
        logger.Err(failMessage);
    }

    void SetFailedSector(sector_t sector, const std::string &failMessage)
    {
        m_failCount++;

        if (!m_failedRanges.empty()) {
            SRange &lastRange = m_failedRanges[m_failedRanges.size()-1];
            if ((lastRange.sector + lastRange.count) == sector) {
                lastRange.count++;
                //LogSector(sector, failMessage);
                return;
            }
        }

        m_failedRanges.emplace_back(sector, 1);
        LogSector(sector, failMessage);
    };
};

/**
 * Fill the contents of the block device with special test data.
 */
void FillAll(const std::shared_ptr<CTestSectorGenetor> ptrGen,
                     const std::shared_ptr<CBlockDevice>& ptrBdev)
{
    AlignedBuffer<unsigned char> portion(SECTOR_SIZE, 1024*1024);
    off_t sizeBdev = ptrBdev->Size();
    sector_t sector = 0;

    logger.Info("device [" + ptrBdev->Name() + "] size " + std::to_string(ptrBdev->Size()) + " bytes");
    for (off_t offset = 0; offset < sizeBdev; offset += portion.Size()) {
        size_t portionSize = std::min(portion.Size(), static_cast<size_t>(sizeBdev-offset));

        ptrGen->Generate(portion.Data(), portionSize, sector);

        ptrBdev->Write(portion.Data(), portionSize, offset);

        sector += (portionSize >> SECTOR_SHIFT);
    }
}

/**
 * Fill the contents of the block device with special test data.
 */
void CheckAll(const std::shared_ptr<CTestSectorGenetor> ptrGen,
              const std::shared_ptr<CBlockDevice>& ptrBdev,
              const int seqNumber, const clock_t seqTime)
{
    AlignedBuffer<unsigned char> portion(SECTOR_SIZE, 1024*1024);
    off_t sizeBdev = ptrBdev->Size();
    sector_t sector = 0;

    for (off_t offset = 0; offset < sizeBdev; offset += portion.Size()) {
        size_t portionSize = std::min(portion.Size(), static_cast<size_t>(sizeBdev-offset));

        ptrBdev->Read(portion.Data(), portionSize, offset);

        ptrGen->Check(portion.Data(), portionSize, sector, seqNumber, seqTime);

        sector += (portionSize >> SECTOR_SHIFT);
    }

    if (ptrGen->Fails() > 0) {
        ptrGen->ShowFails();
        throw std::runtime_error(std::to_string(ptrGen->Fails()) + " corrupted sectors were found");
    }
}

void FillBlocks(const std::shared_ptr<CTestSectorGenetor> &ptrGen,
                const std::shared_ptr<CBlockDevice> &ptrBdev,
                off_t offset, size_t size)
{
    AlignedBuffer<unsigned char> portion(SECTOR_SIZE, size);

    ptrGen->Generate(portion.Data(), size, offset >> SECTOR_SHIFT);
    ptrBdev->Write(portion.Data(), size, offset);
}

/**
 * Fill some random blocks with random offset
 */
void FillRandomBlocks(const std::shared_ptr<CTestSectorGenetor> &ptrGen,
                      const std::shared_ptr<CBlockDevice> &ptrBdev, const int count)
{
    off_t sizeBdev = ptrBdev->Size();
    size_t totalSize = 0;
    std::stringstream ss;

    logger.Detail(std::string("write "+std::to_string(count)+" transactions"));
    for (int cnt = 0; cnt < count; cnt++)
    {
        size_t size = static_cast<size_t>((CRandomHelper::GenerateInt() & 0x1F) + 1) << SECTOR_SHIFT;
        off_t offset = static_cast<off_t>(CRandomHelper::GenerateInt() + 1) << SECTOR_SHIFT;
        if (offset > (sizeBdev - size))
            offset = offset % (sizeBdev - size);

        ss << (offset >> SECTOR_SHIFT) << ":" << (size >> SECTOR_SHIFT) << " ";
        FillBlocks(ptrGen, ptrBdev, offset, size);
        totalSize += size;
    }
    logger.Detail(ss);

    logger.Info("wrote " + std::to_string(count) + " transactions in total with " + std::to_string(totalSize >> SECTOR_SHIFT) + " sectors");

}

void CheckCorruption(const std::string &origDevName, const std::string &diffStorage, const int durationLimitSec)
{
    logger.Info("--- Test: check corruption ---");
    logger.Info("device: " + origDevName);
    logger.Info("diffStorage: " + diffStorage);
    logger.Info("duration: " + std::to_string(durationLimitSec) + " seconds");

    auto ptrGen = std::make_shared<CTestSectorGenetor>();
    auto ptrOrininal = std::make_shared<CBlockDevice>(origDevName);

    logger.Info("-- Fill original device collection by test pattern");
    FillAll(ptrGen, ptrOrininal);

    std::vector<std::string> devices;
    devices.push_back(origDevName);

    std::time_t startTime = std::time(nullptr);
    int elapsed;
    bool isErrorFound = false;
    while (((elapsed = (std::time(nullptr) - startTime)) < durationLimitSec) && !isErrorFound) {
        logger.Info("-- Elapsed time: "+std::to_string(elapsed)+" seconds");
        logger.Info("-- Create snapshot");
        auto ptrSession = CreateBlksnapSession(devices, diffStorage);
        int testSeqNumber = ptrGen->GetSequenceNumber();
        clock_t testSeqTime = std::clock();
        logger.Info("test sequence time "+std::to_string(testSeqTime));

        std::string imageDevName = ptrSession->GetImageDevice(origDevName);
        logger.Info("Found image block device ["+imageDevName+"]");
        auto ptrImage = std::make_shared<CBlockDevice>(imageDevName);

        logger.Info("- Check image content before writing to original device");
        CheckAll(ptrGen, ptrImage, testSeqNumber, testSeqTime);

        //ptrGen->IncSequence();
        //FillBlocks(ptrGen, ptrOrininal, 0, 4096);
        //FillBlocks(ptrGen, ptrOrininal, (ptrOrininal->Size() / 8) & ~(SECTOR_SIZE - 1), 4096);
        //FillBlocks(ptrGen, ptrOrininal, (ptrOrininal->Size() / 4) & ~(SECTOR_SIZE - 1), 4096);
        //FillBlocks(ptrGen, ptrOrininal, (ptrOrininal->Size() / 2) & ~(SECTOR_SIZE - 1), 4096);

        //logger.Info("- Check image content after writing fixed data to original device");
        //CheckAll(ptrGen, ptrImage, testSeqNumber);

        FillBlocks(ptrGen, ptrOrininal, 0, 4096);//
        FillBlocks(ptrGen, ptrOrininal, 4096, 4096);//
        FillBlocks(ptrGen, ptrOrininal, 4096 + 4096, 4096);//

        std::time_t startFillRandom = std::time(nullptr);
        do {
            logger.Info("- Fill some random blocks");
            ptrGen->IncSequence();
            FillRandomBlocks(ptrGen, ptrOrininal, CRandomHelper::GenerateInt() & 0x3FFF/*0x3FF*/);

            FillBlocks(ptrGen, ptrOrininal, 0, 4096); //

            logger.Info("- Check image corruption");
            CheckAll(ptrGen, ptrImage, testSeqNumber, testSeqTime);
        } while ((std::time(nullptr) - startFillRandom) < 30);


        std::string errorMessage;
        while (ptrSession->GetError(errorMessage)) {
            isErrorFound = true;
            logger.Err(errorMessage);
        }
    }

    logger.Info("--- Success: check corruption ---");
}

struct SGeneratorContext
{
    std::atomic_bool stop;
    std::mutex lock;
    std::list<std::string> errorMessages;
    std::shared_ptr<CBlockDevice> ptrBdev;
    std::shared_ptr<CTestSectorGenetor> ptrGen;

    SGeneratorContext(const std::shared_ptr<CBlockDevice> &in_ptrBdev,
                      const std::shared_ptr<CTestSectorGenetor> &in_ptrGen)
        : stop(false)
        , ptrBdev(in_ptrBdev)
        , ptrGen(in_ptrGen)
    {};
};

void GeneratorThreadFunction(std::shared_ptr<SGeneratorContext> ptrCtx)
{
    try {
        logger.Info("- Start writing to device [" + ptrCtx->ptrBdev->Name() + "].");
        while (!ptrCtx->stop) {
            FillRandomBlocks(ptrCtx->ptrGen, ptrCtx->ptrBdev, CRandomHelper::GenerateInt() & 0x3F);
            std::this_thread::sleep_for(std::chrono::milliseconds((CRandomHelper::GenerateInt() & 0x1FF)));
        }
        logger.Info("- Stop writing to device [" + ptrCtx->ptrBdev->Name() + "].");
    }
    catch(std::exception &ex) {
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

    SCheckerContext(const std::shared_ptr<CBlockDevice> &in_ptrBdev,
                    const std::shared_ptr<CTestSectorGenetor> &in_ptrGen)
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
    try {
        logger.Info("- Start checking image device [" + ptrCtx->ptrBdev->Name() + "].");
        CheckAll(ptrCtx->ptrGen, ptrCtx->ptrBdev, ptrCtx->testSeqNumber, ptrCtx->testSeqTime);
        logger.Info("- Complete checking image device [" + ptrCtx->ptrBdev->Name() + "].");
    }
    catch(std::exception &ex) {
        logger.Err(ex.what());

        std::lock_guard<std::mutex> guard(ptrCtx->lock);
        ptrCtx->errorMessages.emplace_back(ex.what());
    }
    ptrCtx->complete = true;
};

void MultithreadCheckCorruption(const std::vector<std::string> &origDevNames, const std::string &diffStorage, const int durationLimitSec)
{
    std::map<std::string, std::shared_ptr<CTestSectorGenetor>> genMap;
    std::vector<std::thread> genThreads;
    std::vector<std::shared_ptr<SGeneratorContext>> genCtxs;

    logger.Info("--- Test: multithread check corruption ---");
    {
        std::string mess("devices:");
        for (const std::string &origDevName : origDevNames)
            mess += std::string(" " + origDevName);
        logger.Info(mess);
    }
    logger.Info("diffStorage: " + diffStorage);
    logger.Info("duration: " + std::to_string(durationLimitSec) + " seconds");

    for (const std::string &origDevName : origDevNames)
        genMap[origDevName] = std::make_shared<CTestSectorGenetor>();

    for (const std::string &origDevName : origDevNames)
        genCtxs.push_back(
            std::make_shared<SGeneratorContext>(
                std::make_shared<CBlockDevice>(origDevName),
                genMap[origDevName]
            )
        );

    // Initiate block device content for each original device
    logger.Info("-- Fill original device collection by test pattern");
    for (const std::shared_ptr<SGeneratorContext> &ptrCtx : genCtxs)
        FillAll(ptrCtx->ptrGen, ptrCtx->ptrBdev);

    // start generators for all original device
    logger.Info("-- Start generation random content to original devices ");
    for (const std::shared_ptr<SGeneratorContext> &ptrCtx : genCtxs)
        genThreads.emplace_back(GeneratorThreadFunction, ptrCtx);

    std::time_t startTime = std::time(nullptr);
    int elapsed;
    bool isErrorFound = false;
    while (((elapsed = (std::time(nullptr) - startTime)) < durationLimitSec) && !isErrorFound) {
        logger.Info("-- Elapsed time: " + std::to_string(elapsed) + " seconds");

        logger.Info("-- Create snapshot at " + std::to_string(std::clock()) + " by CPU clock");
        auto ptrSession = CreateBlksnapSession(origDevNames, diffStorage);

        // Create checker contexts
        std::vector<std::shared_ptr<SCheckerContext>> checkerCtxs;
        for (const std::string &origDevName : origDevNames) {
            std::shared_ptr<SCheckerContext> ptrCtx =
                std::make_shared<SCheckerContext>(
                    std::make_shared<CBlockDevice>(ptrSession->GetImageDevice(origDevName)),
                    genMap[origDevName]
                );

            checkerCtxs.push_back(ptrCtx);
        }

        // Start checker threads
        std::vector<std::thread> checkThreads;
        for (const std::shared_ptr<SCheckerContext> &ptrCtx : checkerCtxs)
        {
            checkThreads.emplace_back(CheckerThreadFunction, ptrCtx);
            logger.Info("Checker thread is started for a block device [" + ptrCtx->ptrBdev->Name() + "]");
        }

        // Waiting for all check threads completed
        logger.Info("-- Waiting for all check threads completed");
        int completeCounter = checkerCtxs.size();
        while (completeCounter) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            logger.Info("-- Still waiting ...");
            for (const std::shared_ptr<SCheckerContext> &ptrCtx : checkerCtxs) {
                if (ptrCtx->complete && !ptrCtx->processed) {
                    if (ptrCtx->errorMessages.size() == 0)
                        logger.Info("- Successfully checking complete for device [" + ptrCtx->ptrBdev->Name() + "].");
                    else {
                        isErrorFound = true;
                        std::stringstream ss;

                        ss << "- Checking for device [" << ptrCtx->ptrBdev->Name() << "] failed." << std::endl;
                        while (ptrCtx->errorMessages.size()) {
                            ss << ptrCtx->errorMessages.front() << std::endl;
                            ptrCtx->errorMessages.pop_front();
                        }
                        logger.Err(ss);
                    }
                    ptrCtx->processed = true;
                    completeCounter--;
                }
            }
        }

        for (auto &checkThread : checkThreads)
            checkThread.join();

        logger.Info("-- Check errors");

        // Check errors from BlksnapSession
        std::string errorMessage;
        while (ptrSession->GetError(errorMessage)) {
            isErrorFound = true;
            logger.Err(errorMessage);
        }

        // Check errors from generator threads
        for (const std::shared_ptr<SGeneratorContext> &ptrCtx : genCtxs) {
            std::lock_guard<std::mutex> guard(ptrCtx->lock);

            while (ptrCtx->errorMessages.size()) {
                isErrorFound = true;

                logger.Err(ptrCtx->errorMessages.front());
                ptrCtx->errorMessages.pop_front();
            }
        }

        // clear resources
        checkThreads.clear();
        checkerCtxs.clear();
        ptrSession.reset();
    }

    // stop generators
    logger.Info("-- Stop generation random content to original devices");
    for (const std::shared_ptr<SGeneratorContext> &ptrCtx : genCtxs)
        ptrCtx->stop = true;

    for (auto &genThread : genThreads)
        genThread.join();

    if (isErrorFound)
        throw std::runtime_error("--- Failed: multithread check corruption ---");

    logger.Info("--- Success: multithread check corruption ---");
}

void Main(int argc, char *argv[])
{
    po::options_description desc;
    std::string usage = std::string("Checking the correctness of the COW algorithm of the blksnap module.");

    desc.add_options()
        ("help,h", "Show usage information.")
        ("log,l", po::value<std::string>(), "Detailed log of all transactions.")
        ("device,d", po::value<std::vector<std::string>>()->multitoken(), "Device name. It's multitoken for multithread test mod.")
        ("diff_storage,s", po::value<std::string>(), "Directory name for allocating diff storage files.")
        ("multithread", "Testing mode in which writings to the original devices and their checks are performed in parallel.")
        ("duration,u", po::value<int>(), "The test duration limit in minutes.");
    po::variables_map vm;
    po::parsed_options parsed = po::command_line_parser(argc, argv).options(desc).run();
    po::store(parsed, vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << usage << std::endl;
        std::cout << desc << std::endl;
        return;
    }

    if (vm.count("log")){
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

    if (!!vm.count("multithread"))
        MultithreadCheckCorruption(origDevNames, diffStorage, duration*60);
    else {
        if (origDevNames.size() > 1)
            logger.Err("In singlethread test mode used only first device.");

        CheckCorruption(origDevNames[0], diffStorage, duration*60);
    }
}

int main(int argc, char *argv[])
{
#if 0
    Main(argc, argv);
#else
    try
    {
        Main(argc, argv);
    }
    catch(std::exception& ex)
    {
        logger.Err(ex.what());
        return 1;
    }
#endif
    return 0;
}
