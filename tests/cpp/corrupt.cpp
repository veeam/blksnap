#include <ctime>
#include <iostream>
#include <cstring>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <zlib.h>

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

/**
 * Algorithm:
 * Заполняем содержимое блочного устройства (задаётся параметром) специальными тестовыми данными.
 * Создаём снапшот.
 * Проверяем, что на образе данные корректны.
 * В цикле перезаписываем случайное количество случайных блоков на устройстве.
 * Проверяем, что на образе данные не изменились.
 * Выполняем эту проверку в течении заданного времени (задаётся параметром).
 *
 *
 */

struct STestHeader
{
    int crc;
    int seq_number;
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
    {

    };
    ~CTestSectorGenetor()
    {

    };

    void IncSequenceNumber()
    {
        m_seqNumber++;
    };

    void Generate(unsigned char *buffer, size_t size, sector_t sector)
    {
        for (size_t offset = 0; offset < size; offset += SECTOR_SIZE) {
            STestSector *current = (STestSector *)(buffer + offset);

            CRandomHelper::GenerateBuffer(current->body, sizeof(current->body));

            STestHeader *header = &current->header;
            header->seq_number = m_seqNumber;
            header->sector = sector;
            header->crc = crc32(0, buffer + offset + sizeof(header->crc), SECTOR_SIZE-sizeof(header->crc));

            sector++;
        }
    };

    void Check(unsigned char *buffer, size_t size, sector_t sector)
    {
        for (size_t offset = 0; offset < size; offset += SECTOR_SIZE) {
            struct STestSector *current = (STestSector *)buffer;
            STestHeader *header = &current->header;
            int crc = crc32(0, buffer+sizeof(header->crc), SECTOR_SIZE-sizeof(header->crc));

            bool isCorrupted = (crc != header->crc);
            bool isIncorrect = (sector != header->sector);

            if (isCorrupted || isIncorrect) {
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

    void ShowFails()
    {
        std::cerr << m_failCount << " corrupted sectors" << std::endl;
        std::cerr << failedRanges.size() << " corrupted ranges" << std::endl;
        std::cerr << "Ranges of corrupted sectors:" << std::endl;
        for (const SRange &range : failedRanges)
            std::cerr << range.sector << ":" << range.count << std::endl;
    };

private:
    int m_seqNumber;
    int m_failCount;
    int m_logLineCount;
    std::vector<SRange> failedRanges;

private:
    void LogSector(sector_t sector, const std::string &failMessage)
    {
        if (failMessage.empty())
            return;

        m_logLineCount++;
        std::cerr << failMessage << std::endl;
    }

    void SetFailedSector(sector_t sector, const std::string &failMessage)
    {
        m_failCount++;

        if (!failedRanges.empty()) {
            SRange &lastRange = failedRanges[failedRanges.size()-1];
            if ((lastRange.sector + lastRange.count) == sector) {
                lastRange.count++;
                //LogSector(sector, failMessage);
                return;
            }
        }

        failedRanges.emplace_back(sector, 1);
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

    std::cout << "device [" << ptrBdev->Name() << "] size " << ptrBdev->Size() << " bytes" << std::endl;
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
                      const std::shared_ptr<CBlockDevice>& ptrBdev)
{
    AlignedBuffer<unsigned char> portion(SECTOR_SIZE, 1024*1024);
    off_t sizeBdev = ptrBdev->Size();
    sector_t sector = 0;

    for (off_t offset = 0; offset < sizeBdev; offset += portion.Size()) {
        size_t portionSize = std::min(portion.Size(), static_cast<size_t>(sizeBdev-offset));

        ptrBdev->Read(portion.Data(), portionSize, offset);

        ptrGen->Check(portion.Data(), portionSize, sector);

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

    std::cout << "Write sectors:"<< (offset >> SECTOR_SHIFT) << ":" << (size >> SECTOR_SHIFT) << std::endl;
    ptrGen->Generate(portion.Data(), size, offset >> SECTOR_SHIFT);
    ptrBdev->Write(portion.Data(), size, offset);
}

/**
 * Fill some random blocks with random offset
 */
void FillRandomBlocks(const std::shared_ptr<CTestSectorGenetor> &ptrGen,
                      const std::shared_ptr<CBlockDevice> &ptrBdev)
{
    off_t sizeBdev = ptrBdev->Size();
    int count = CRandomHelper::GenerateInt() & 0xF/*0x3F*/;

    for (; count > 0; count--)
    {
        size_t portionSize = static_cast<size_t>((CRandomHelper::GenerateInt() & 0x1F) + 1) << SECTOR_SHIFT;
        off_t offset = static_cast<off_t>(CRandomHelper::GenerateInt() + 1) << SECTOR_SHIFT;
        if (offset > (sizeBdev - portionSize))
            offset = offset % (sizeBdev - portionSize);

        FillBlocks(ptrGen, ptrBdev, offset, portionSize);
    }
}

void CheckCorruption(const std::string &origDevName, const std::string &diffStorage, const int durationLimitSec)
{
    std::cout << "--- Test: check corruption ---" << std::endl;
    std::cout << "device: " << origDevName << std::endl;
    std::cout << "diffStorage: " << diffStorage << std::endl;
    std::cout << "duration: " << durationLimitSec << " seconds" << std::endl;

    auto ptrGen = std::make_shared<CTestSectorGenetor>();
    auto ptrOrininal = std::make_shared<CBlockDevice>(origDevName);

    std::cout << "Fill original device collection by test pattern " << std::endl;
    FillAll(ptrGen, ptrOrininal);

    std::vector<std::string> devices;
    devices.push_back(origDevName);
    std::cout << "Create snapshot" << std::endl;
    auto ptrSession = CreateBlksnapSession(devices, diffStorage);

    std::string imageDevName = ptrSession->GetImageDevice(origDevName);
    std::cout << "Found image block device "<< imageDevName << std::endl;
    auto ptrImage = std::make_shared<CBlockDevice>(imageDevName);

    std::cout << "Check image content before writing to original device" << std::endl;
    CheckAll(ptrGen, ptrImage);

    FillBlocks(ptrGen, ptrOrininal, 0, 4096);
    FillBlocks(ptrGen, ptrOrininal, (ptrOrininal->Size() / 8) & ~(SECTOR_SIZE - 1), 4096);
    FillBlocks(ptrGen, ptrOrininal, (ptrOrininal->Size() / 4) & ~(SECTOR_SIZE - 1), 4096);
    FillBlocks(ptrGen, ptrOrininal, (ptrOrininal->Size() / 2) & ~(SECTOR_SIZE - 1), 4096);

    std::cout << "Check image content after writing fixed data to original device" << std::endl;
    CheckAll(ptrGen, ptrImage);

    std::time_t startTime = std::time(nullptr);
    int elapsed = (std::time(nullptr) - startTime);
    while (elapsed < durationLimitSec) {
        std::cout << "Elapsed time: "<< elapsed << "seconds" << std::endl;

        std::cout << "Fill some random blocks" << std::endl;
        FillRandomBlocks(ptrGen, ptrOrininal);

        std::cout << "Check image corruption" << std::endl;
        CheckAll(ptrGen, ptrImage);
        elapsed = (std::time(nullptr) - startTime);
    }

    std::cout << "--- Complete: check corruption ---" << std::endl;
}

void Main(int argc, char *argv[])
{
    po::options_description desc;
    std::string usage = std::string("Checking the correctness of the COW algorithm of the blksnap module.");

    desc.add_options()
        ("help,h", "Show usage information.")
        ("device,d", po::value<std::string>(), "Device name.")
        ("diff_storage,s", po::value<std::string>(), "Directory name for allocating diff storage files.");
    po::variables_map vm;
    po::parsed_options parsed = po::command_line_parser(argc, argv).options(desc).run();
    po::store(parsed, vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << usage << std::endl;
        std::cout << desc << std::endl;
        return;
    }

    if (!vm.count("device"))
        throw std::invalid_argument("Argument 'device' is missed.");
    std::string origDevName = vm["device"].as<std::string>();

    if (!vm.count("diff_storage"))
        throw std::invalid_argument("Argument 'diff_storage' is missed.");
    std::string diffStorage = vm["diff_storage"].as<std::string>();

    CheckCorruption(origDevName, diffStorage, 5*60);
}

int main(int argc, char *argv[])
{
    try
    {
        Main(argc, argv);
    }
    catch(std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
