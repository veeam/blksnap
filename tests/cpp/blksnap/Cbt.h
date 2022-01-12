/*
 * The hi-level abstraction for the blksnap kernel module.
 * Allows to receive data from CBT.
 */
#include <vector>
#include <memory>
#include <uuid/uuid.h>
#include "DeviceId.h"

namespace blksnap
{

struct SCbtInfo
{
    SCbtInfo(){};
    SCbtInfo(const SDeviceId inOriginalDevId, const uint32_t inBlockSize,
        const uint32_t inBlockCount, const uint64_t inDeviceCapacity,
        const  uuid_t &inGenerationId, const uint8_t inSnapNumber)
        : originalDevId(inOriginalDevId)
        , blockSize(inBlockSize)
        , blockCount(inBlockCount)
        , deviceCapacity(inDeviceCapacity)
        , snapNumber(inSnapNumber)
    {
        uuid_copy(generationId, inGenerationId);
    };
    ~SCbtInfo(){};

    SDeviceId originalDevId;
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
