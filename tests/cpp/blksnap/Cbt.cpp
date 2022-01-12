#include "Cbt.h"
#include "Blksnap.h"

using namespace blksnap;

class CCbt : public ICbt
{
public:
    CCbt();
    ~CCbt() override {};

    std::shared_ptr<SCbtInfo> GetCbtInfo(const std::string &original) override;
    std::shared_ptr<SCbtData> GetCbtData(const std::shared_ptr<SCbtInfo> &ptrCbtInfo) override;

private:
    const struct blk_snap_cbt_info &GetCbtInfoInternal(const SDeviceId & devId);

private:
    CBlksnap m_blksnap;
    std::vector<struct blk_snap_cbt_info> m_cbtInfos;
};

std::shared_ptr<ICbt> ICbt::Create()
{
    return std::make_shared<CCbt>();
}


CCbt::CCbt()
{
    m_blksnap.CollectTrackers(m_cbtInfos);
}

const struct blk_snap_cbt_info & CCbt::GetCbtInfoInternal(const SDeviceId & devId)
{
    for (const struct blk_snap_cbt_info & cbtInfo : m_cbtInfos)
        if (devId == SDeviceId(cbtInfo.dev_id.mj, cbtInfo.dev_id.mn))
            return cbtInfo;

    throw std::runtime_error("The device [" + devId.ToString() + "] was not found in the CBT table");
}

std::shared_ptr<SCbtInfo> CCbt::GetCbtInfo(const std::string &original)
{
    SDeviceId devId = SDeviceId::DeviceByName(original);
    const struct blk_snap_cbt_info &cbtInfo = GetCbtInfoInternal(devId);

    return std::make_shared<SCbtInfo>(devId, cbtInfo.blk_size, cbtInfo.blk_count,
                                      cbtInfo.device_capacity, cbtInfo.generation_id,
                                      cbtInfo.snap_number);
}

std::shared_ptr<SCbtData> CCbt::GetCbtData(const std::shared_ptr<SCbtInfo> &ptrCbtInfo)
{
    struct blk_snap_dev_t originalDevId = {
        .mj = ptrCbtInfo->originalDevId.mj,
        .mn = ptrCbtInfo->originalDevId.mn
    };
    auto ptrCbtMap = std::make_shared<SCbtData>(ptrCbtInfo->blockCount);

    m_blksnap.ReadCbtMap(originalDevId, 0, ptrCbtMap->vec.size(), ptrCbtMap->vec.data());

    return ptrCbtMap;
}
