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

#include <string>
#include <uuid/uuid.h>
#include <vector>

#if 0
#ifndef BLKSNAP_MODIFICATION
/* Allow to use additional IOCTL from module modification */
#    define BLKSNAP_MODIFICATION
/* Allow to get any sector state. Can be used only for debug purpose */
#    define BLKSNAP_DEBUG_SECTOR_STATE
#endif
#endif
#include "Sector.h"
#include <linux/blk-filter.h>
#include <linux/blksnap.h>

namespace blksnap
{
    class CTrackerCtl
    {
    public:
        CTrackerCtl(const std::string& devicePath);
        ~CTrackerCtl();

        bool Attach();
        void Detach();

        void CbtInfo(struct blksnap_cbtinfo& cbtInfo);
        void ReadCbtMap(unsigned int offset, unsigned int length, uint8_t* buff);
        void MarkDirtyBlock(std::vector<struct blksnap_sectors>& ranges);
        void SnapshotAdd(const uuid_t& id);
        void SnapshotInfo(struct blksnap_snapshotinfo& snapshotinfo);

    private:
        int m_fd;
    };

}
