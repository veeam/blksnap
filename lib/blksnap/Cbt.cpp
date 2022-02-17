/* [TBD]
 * Copyright (C) 2022 Veeam Software Group GmbH <https://www.veeam.com/contacts.html>
 *
 * This file is part of libblksnap
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Lesser Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <blksnap/Blksnap.h>
#include <blksnap/Cbt.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <system_error>

using namespace blksnap;

class CCbt : public ICbt
{
public:
    CCbt();
    ~CCbt() override{};

    std::shared_ptr<SCbtInfo> GetCbtInfo(const std::string& original) override;
    std::shared_ptr<SCbtData> GetCbtData(const std::shared_ptr<SCbtInfo>& ptrCbtInfo) override;

private:
    const struct blk_snap_cbt_info& GetCbtInfoInternal(unsigned int mj, unsigned int mn);

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

const struct blk_snap_cbt_info& CCbt::GetCbtInfoInternal(unsigned int mj, unsigned int mn)
{
    for (const struct blk_snap_cbt_info& cbtInfo : m_cbtInfos)
        if ((mj == cbtInfo.dev_id.mj) && (mn == cbtInfo.dev_id.mn))
            return cbtInfo;

    throw std::runtime_error("The device [" + std::to_string(mj) + ":" + std::to_string(mn)
                             + "] was not found in the CBT table");
}

std::shared_ptr<SCbtInfo> CCbt::GetCbtInfo(const std::string& original)
{
    struct stat st;

    if (::stat(original.c_str(), &st))
        throw std::system_error(errno, std::generic_category(), original);

    const struct blk_snap_cbt_info& cbtInfo = GetCbtInfoInternal(major(st.st_rdev), minor(st.st_rdev));

    return std::make_shared<SCbtInfo>(major(st.st_rdev), minor(st.st_rdev), cbtInfo.blk_size, cbtInfo.blk_count,
                                      cbtInfo.device_capacity, cbtInfo.generation_id, cbtInfo.snap_number);
}

std::shared_ptr<SCbtData> CCbt::GetCbtData(const std::shared_ptr<SCbtInfo>& ptrCbtInfo)
{
    struct blk_snap_dev_t originalDevId = {.mj = ptrCbtInfo->originalMajor, .mn = ptrCbtInfo->originalMinor};
    auto ptrCbtMap = std::make_shared<SCbtData>(ptrCbtInfo->blockCount);

    m_blksnap.ReadCbtMap(originalDevId, 0, ptrCbtMap->vec.size(), ptrCbtMap->vec.data());

    return ptrCbtMap;
}
