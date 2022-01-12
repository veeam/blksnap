/*
 * The hi-level abstraction for the blksnap kernel module.
 * Allows to receive data from CBT.
 */
#include <vector>
#include <memory>
#include <uuid/uuid.h>

namespace blksnap
{

struct SCbtInfo
{
    SCbtInfo(){};
    SCbtInfo(
        const unsigned int inOriginalMajor, const unsigned int inOriginalMinor,
        const uint32_t inBlockSize, const uint32_t inBlockCount,
        const uint64_t inDeviceCapacity,
        const uuid_t &inGenerationId, const uint8_t inSnapNumber)
        : originalMajor(inOriginalMajor)
        , originalMinor(inOriginalMinor)
        , blockSize(inBlockSize)
        , blockCount(inBlockCount)
        , deviceCapacity(inDeviceCapacity)
        , snapNumber(inSnapNumber)
    {
        uuid_copy(generationId, inGenerationId);
    };
    ~SCbtInfo(){};

    unsigned int originalMajor;
    unsigned int originalMinor;
    unsigned int blockSize;
    unsigned int blockCount;
    unsigned long long deviceCapacity;
    uuid_t generationId;
    uint8_t snapNumber;
};

struct SCbtData
{
    SCbtData(size_t blockCount)
    {
        vec.resize(blockCount);
    };
    ~SCbtData() {};

    std::vector<uint8_t> vec;
};

struct ICbt
{
    virtual ~ICbt() {};

    virtual std::shared_ptr<SCbtInfo> GetCbtInfo(const std::string &original) = 0;
    virtual std::shared_ptr<SCbtData> GetCbtData(const std::shared_ptr<SCbtInfo> &ptrCbtInfo) = 0;

    static std::shared_ptr<ICbt> Create();
};

}
