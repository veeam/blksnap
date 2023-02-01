/*
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
#include <blksnap/TrackerCtl.h>
#include <blksnap/Cbt.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <system_error>

using namespace blksnap;

class CCbt : public ICbt
{
public:
    CCbt(const std::string& devicePath)
        : m_ctl(devicePath)
    {};
    ~CCbt() override
    {};

    std::string GetImage() override
    {
        struct blksnap_snapshotinfo snapshotinfo;

        m_ctl.SnapshotInfo(snapshotinfo);

        std::string name("/dev/");
        for (int inx = 0; (inx < IMAGE_DISK_NAME_LEN) && (snapshotinfo.image[inx] != '\0'); inx++)
            name += static_cast<char>(snapshotinfo.image[inx]);

        return name;
    }

    int GetError() override
    {
        struct blksnap_snapshotinfo snapshotinfo;

        m_ctl.SnapshotInfo(snapshotinfo);
        return snapshotinfo.error_code;
    };

    std::shared_ptr<SCbtInfo> GetCbtInfo() override
    {
        struct blksnap_cbtinfo cbtInfo;

        m_ctl.CbtInfo(cbtInfo);

        return std::make_shared<SCbtInfo>(
            cbtInfo.block_size,
            cbtInfo.block_count,
            cbtInfo.device_capacity,
            cbtInfo.generation_id.b,
            cbtInfo.changes_number);
    };

    std::shared_ptr<SCbtData> GetCbtData() override
    {
        struct blksnap_cbtinfo cbtInfo;
        m_ctl.CbtInfo(cbtInfo);

        auto ptrCbtMap = std::make_shared<SCbtData>(cbtInfo.block_count);
        m_ctl.ReadCbtMap(0, ptrCbtMap->vec.size(), ptrCbtMap->vec.data());

        return ptrCbtMap;
    };
private:
    CTrackerCtl m_ctl;
};

std::shared_ptr<ICbt> ICbt::Create(const std::string& devicePath)
{
    return std::make_shared<CCbt>(devicePath);
}

