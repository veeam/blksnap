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

#include <stdint.h>
#include <string>
#include <uuid/uuid.h>
#include <vector>

#ifndef BLK_SNAP_MODIFICATION
/* Allow to use additional IOCTL from module modification */
#    define BLK_SNAP_MODIFICATION
/* Allow to get any sector state. Can be used only for debug purpose */
#    define BLK_SNAP_DEBUG_SECTOR_STATE
#endif
#include "Sector.h"
#include "blk_snap.h"

namespace blksnap
{
    struct SBlksnapEventLowFreeSpace
    {
        unsigned long long requestedSectors;
    };

    struct SBlksnapEventCorrupted
    {
        struct blk_snap_dev_t origDevId;
        int errorCode;
    };

    struct SBlksnapEvent
    {
        unsigned int code;
        long long time;
        union
        {
            SBlksnapEventLowFreeSpace lowFreeSpace;
            SBlksnapEventCorrupted corrupted;
        };
    };

    class CBlksnap
    {
    public:
        CBlksnap();
        ~CBlksnap();

        void Version(struct blk_snap_version& version);
        void CollectTrackers(std::vector<struct blk_snap_cbt_info>& cbtInfoVector);
        void ReadCbtMap(struct blk_snap_dev_t dev_id, unsigned int offset, unsigned int length, uint8_t* buff);

        void Create(const std::vector<struct blk_snap_dev_t>& devices, uuid_t& id);
        void Destroy(const uuid_t& id);
        void Collect(const uuid_t& id, std::vector<struct blk_snap_image_info>& images);
        void AppendDiffStorage(const uuid_t& id, const struct blk_snap_dev_t& dev_id,
                               const std::vector<struct blk_snap_block_range>& ranges);
        void Take(const uuid_t& id);
        bool WaitEvent(const uuid_t& id, unsigned int timeoutMs, SBlksnapEvent& ev);

#ifdef BLK_SNAP_MODIFICATION
        /* Additional functional */
        bool Modification(struct blk_snap_mod& mod);
#    ifdef BLK_SNAP_DEBUG_SECTOR_STATE
        void GetSectorState(struct blk_snap_dev_t image_dev_id, off_t offset, struct blk_snap_sector_state& state);
#    endif
#endif
    private:
        int m_fd;
    };

}
