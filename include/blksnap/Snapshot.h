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
#pragma once
/*
 * The low-level abstraction over ioctl for the blksnap kernel module.
 * Allows to interact with the module with minimal overhead and maximum
 * flexibility. Uses structures that are directly passed to the kernel module.
 */

#include <memory>
#include <string>
#include <uuid/uuid.h>
#include <cstring>
#include <vector>

#include "Sector.h"
#include <linux/fs.h>
#include <linux/blksnap.h>

namespace blksnap
{
    struct SBlksnapEventCorrupted
    {
        unsigned int origDevIdMj;
        unsigned int origDevIdMn;
        int errorCode;
    };

    struct SBlksnapEvent
    {
        unsigned int code;
        long long time;
        SBlksnapEventCorrupted corrupted;
    };

    class CSnapshotId
    {
    public:
        CSnapshotId()
        {
            uuid_clear(m_id);
        };
        CSnapshotId(const uuid_t& id)
        {
            uuid_copy(m_id, id);
        };
        CSnapshotId(const __u8 buf[16])
        {
            memcpy(m_id, buf, sizeof(uuid_t));
        };
        CSnapshotId(const std::string& idStr)
        {
            uuid_parse(idStr.c_str(), m_id);
        };

        void FromString(const std::string& idStr)
        {
            uuid_parse(idStr.c_str(), m_id);
        };
        const uuid_t& Get() const
        {
            return m_id;
        };
        std::string ToString() const
        {
            char idStr[64];

            uuid_unparse(m_id, idStr);

            return std::string(idStr);
        };
    private:
        uuid_t m_id;
    };

    class OpenFileHolder
    {
    public:
        OpenFileHolder(const std::string& filename, int flags, int mode = 0);
        ~OpenFileHolder();
        int Get();

    private:
        int m_fd;
    };

    class CSnapshot
    {
    public:
        static void Collect(std::vector<CSnapshotId>& ids);
        static void Version(struct blksnap_version& version);
        static std::shared_ptr<CSnapshot> Create(const std::string& filePath, const unsigned long long limit);
        static std::shared_ptr<CSnapshot> Open(const CSnapshotId& id);

    public:
        virtual ~CSnapshot() {};

        void Take();
        void Destroy();
        bool WaitEvent(unsigned int timeoutMs, SBlksnapEvent& ev);

        const uuid_t& Id() const
        {
            return m_id.Get();
        }
    private:
        CSnapshot(const CSnapshotId& id, const std::shared_ptr<OpenFileHolder>& ctl);

        CSnapshotId m_id;
        std::shared_ptr<OpenFileHolder> m_ctl;
    };

}
