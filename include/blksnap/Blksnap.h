/* [TBD]
 * Copyright (C) 2022 Veeam Software Group GmbH <https://www.veeam.com/contacts.html>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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

#ifndef BLK_SNAP_MODIFICATION
/* Allow to use additional IOCTL from module modification */
#    define BLK_SNAP_MODIFICATION
/* Allow to get any sector state. Can be used only for debug purpose */
#    define BLK_SNAP_DEBUG_SECTOR_STATE
#endif
#include "blk_snap.h"

#ifndef SECTOR_SHIFT
#    define SECTOR_SHIFT 9
#endif
#ifndef SECTOR_SIZE
#    define SECTOR_SIZE (1 << SECTOR_SHIFT)
#endif

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
