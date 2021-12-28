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
    long crc;
    long seq_number;
    sector_t sector;
};

struct STestSector
{
    STestHeader header;
    char body[SECTOR_SIZE-sizeof(STestHeader)];
};

class CTestSectorGenetor
{
public:
    CTestSectorGenetor()
        : m_seqNumber(0)
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
            STestSector *current = (STestSector *)buffer;

            CRandomHelper::GenerateBuffer(current->body, sizeof(current->body));

            STestHeader *header = &current->header;
            header->seq_number = m_seqNumber;
            header->sector = sector;
            header->crc = crc32(0, buffer+sizeof(long), SECTOR_SIZE-sizeof(long));

            sector++;
            buffer += SECTOR_SIZE;
        }
    };

    void Check(unsigned char *buffer, size_t size, sector_t sector)
    {
        for (size_t offset = 0; offset < size; offset += SECTOR_SIZE) {
            struct STestSector *current = (STestSector *)buffer;
            long crc = crc32(0, buffer+sizeof(long), SECTOR_SIZE-sizeof(long));
            STestHeader *header = &current->header;

            if (unlikely(crc != header->crc)) {
                std::cerr << "Corrupted sector" << std::endl;
                std::cerr << "crc " << header->crc << " != " << crc << std::endl;
                std::cerr << "sector " << header->sector << " != " << sector << std::endl;
                std::cerr << "sequence #" << header->seq_number << std::endl;
                throw std::runtime_error("Corrupted sector");
            }

            if (unlikely(sector != header->sector)) {
                std::cerr << "Incorrect sector" << std::endl;
                std::cerr << "sector " << header->sector << " != " << sector << std::endl;
                std::cerr << "sequence #" << header->seq_number << std::endl;
                throw std::runtime_error("Incorrect sector");
            }

            sector++;
            buffer += SECTOR_SIZE;
        }
    };
private:
    long m_seqNumber;
};

/**
 * Fill the contents of the block device with special test data.
 */
void FillAll(const std::shared_ptr<CTestSectorGenetor> ptrGen,
                     const std::shared_ptr<CBlockDevice>& ptrBdev)
{
    std::vector<unsigned char> portion(1024*1024);
    off_t sizeBdev = ptrBdev->Size();
    sector_t sector = 0;

    for (off_t offset = 0; offset < sizeBdev; offset += portion.size()) {
        size_t portionSize = std::min(portion.size(), static_cast<size_t>(sizeBdev-offset));

        ptrGen->Generate(portion.data(), portionSize, sector);

        ptrBdev->Write(portion.data(), portionSize, offset);

        sector += (portionSize >> SECTOR_SHIFT);
    }
}

/**
 * Fill the contents of the block device with special test data.
 */
void CheckAll(const std::shared_ptr<CTestSectorGenetor> ptrGen,
                      const std::shared_ptr<CBlockDevice>& ptrBdev)
{
    std::vector<unsigned char> portion(1024*1024);
    off_t sizeBdev = ptrBdev->Size();
    sector_t sector = 0;

    for (off_t offset = 0; offset < sizeBdev; offset += portion.size()) {
        size_t portionSize = std::min(portion.size(), static_cast<size_t>(sizeBdev-offset));

        ptrBdev->Read(portion.data(), portionSize, offset);

        ptrGen->Check(portion.data(), portionSize, sector);

        sector += (portionSize >> SECTOR_SHIFT);
    }
}


/**
 * Fill some random blocks with random offset
 */
void FillRandomBlocks(const std::shared_ptr<CTestSectorGenetor> ptrGen,
                      const std::shared_ptr<CBlockDevice>& ptrBdev)
{
    off_t sizeBdev = ptrBdev->Size();
    std::vector<unsigned char> portion;
    long count = CRandomHelper::GenerateLong() & 0x3F;

    for (; count > 0; count--)
    {
        size_t portionSize = static_cast<size_t>((CRandomHelper::GenerateLong() & 0x1F) + 1) << SECTOR_SHIFT;
        portion.resize(portionSize);

        off_t offset = static_cast<off_t>(CRandomHelper::GenerateLong() + 1) << SECTOR_SHIFT;
        if (offset > (sizeBdev - portionSize))
            offset = offset % (sizeBdev - portionSize);

        ptrGen->Generate(portion.data(), portionSize, offset >> SECTOR_SHIFT);

        ptrBdev->Write(portion.data(), portionSize, offset);

        std::cout << "Write "<< std::to_string(offset >> SECTOR_SHIFT) << ":"<< std::to_string(portionSize >> SECTOR_SHIFT) << std::endl;
    }
}

void CheckCorruption(const std::string &origDevName, const std::string &diffStorage, const int durationLimitSec)
{
    std::cout << "--- Test: check corruption ---" << std::endl;
    std::cout << "device: " << origDevName << std::endl;
    std::cout << "diffStorage: " << diffStorage << std::endl;
    std::cout << "duration: " << std::to_string(durationLimitSec) << "seconds" << std::endl;

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
        ("device,d", po::value<std::string>(), "[TBD]Device name.")
        ("diff_storage,s", po::value<std::string>(), "[TBD]Directory name for allocating diff storage files.");
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
