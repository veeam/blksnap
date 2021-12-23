#include <cstring>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <zlib.h>

#include "helpers/RandomHelper.h"
#include "helpers/BlockDevice.h"

#include <boost/program_options.hpp>
namespace po = boost::program_options;
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;


#define SECTOR_SIZE 512
#define SECTOR_SHIFT 9
typedef unsigned long long sector_t;

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
    char[SECTOR_SIZE-sizeof(STestHeader)] body;
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
        inc(m_seqNumber)
    };

    void Generate(void *buffer, size_t *size, sector_t sector)
    {
        struct STestSector *current = buffer;

        for (size_t offset = 0; offset < size; offset += SECTOR_SIZE) {
            CRandomHelper::GenerateBuffer(current->body, SECTOR_SIZE-sizeof(STestHeader));
            current->seq_number = m_seqNumber;
            current->sector = sector;
            current->crc = crc32(0, &current->seq_number, SECTOR_SIZE-4);

            sector++;
            current++;
        }

    };

    void Check(void *buffer, size_t *size, sector_t sector)
    {
        struct STestSector *current = buffer;

        for (size_t offset = 0; offset < size; offset += SECTOR_SIZE) {
            long crc = crc32(0, &current->seq_number, SECTOR_SIZE-4);

            if (unlikely(crc != current->crc)) {
                std::cerr << "Corrupted sector" << std::endl;
                std::cerr << "crc " << current->crc << " != " << crc << std::endl;
                std::cerr << "sector " << current->sector << " != " << sector << std::endl;
                std::cerr << "sequence #" << current->seq_number << std::endl;
                throw std::runtime_error("Corrupted sector");
            }

            if (unlikely(sector != current->sector)) {
                std::cerr << "Incorrect sector" << std::endl;
                std::cerr << "sector " << current->sector << " != " << sector << std::endl;
                std::cerr << "sequence #" << current->seq_number << std::endl;
                throw std::runtime_error("Incorrect sector");
            }

            sector++;
            current++;
        }
    };
private:
    long m_seqNumber;
};

/**
 * Fill the contents of the block device with special test data.
 */
void FillInitContent(const std::shared_ptr<CBlockDevice>& ptrBdev)
{
    auto gen = std::make_shared<CTestSectorGenetor>();
    std::vector<char> portion(1024*1024);
    off_t sizeBdev = ptrBdev->Size();
    sector_t sector = 0;

    for (off_t offset = 0; offset < sizeBdev; offset += portion.size()) {
        size_t portionSize = min(portion.size(), sizeBdev-offset);

        gen->Generate(portion.get(), portionSize, sector);

        ptrBdev->Write(portion.get(), portionSize, offset);

        sector += (portionSize >> SECTOR_SHIFT);
    }
}

void CheckCorruption(const std::string &origDevName, const std::string &diffStorage, const int DurationLimitSec)
{
    std::shared_ptr<IBlksnap> ptrBlksnap;
    auto ptrBdev = std::make_shared<CBlockDevice>(origDevName);

    FillInitContent(ptrBdev);

    std::vector<std::string> devices;
    devices.push_back(origDevName);
    ptrBlksnap = CreateBlksnap(devices);

    diffStorage
}

void Main(int argc, char *argv[])
{
    po::options_description desc;
    std::string usage = std::string("Checking the correctness of the COW algorithm of the blksnap module.");

    m_desc.add_options()
        ("device,d", po::value<std::string>(), "[TBD]Device name.")
        ("diff_storage,s", po::value<std::string>(), "[TBD]Directory name for allocating diff storage files.");
    po::variables_map vm;
    po::parsed_options parsed = po::command_line_parser(argc, argv).options(m_desc).run();
    po::store(parsed, vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << m_usage << std::endl;
        std::cout << m_desc << std::endl;
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
