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
#include "Sector.h"
#include "SnapshotId.h"
#include "OpenFileHolder.h"
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

    class CSnapshot
    {
    public:
        static std::shared_ptr<CSnapshot> Create(const std::string& filePath, const unsigned long long limit);
        static std::shared_ptr<CSnapshot> Open(const CSnapshotId& id);

    public:
        virtual ~CSnapshot() {};

        void Take();
        void Destroy();
        bool WaitEvent(unsigned int timeoutMs, SBlksnapEvent& ev);

        const CSnapshotId& Id() const
        {
            return m_id;
        }
    private:
        CSnapshot(const CSnapshotId& id, const std::shared_ptr<COpenFileHolder>& ctl);

        CSnapshotId m_id;
        std::shared_ptr<COpenFileHolder> m_ctl;
    };
}
