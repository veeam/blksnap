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

#ifndef SECTOR_SHIFT
#    define SECTOR_SHIFT 9
#endif
#ifndef SECTOR_SIZE
#    define SECTOR_SIZE (1 << SECTOR_SHIFT)
#endif
#include <string>
#include <vector>

namespace blksnap
{
    typedef unsigned long long sector_t;

    struct SRange
    {
        sector_t sector;
        sector_t count;

        SRange(sector_t inSector, sector_t inCount)
            : sector(inSector)
            , count(inCount) {};
        SRange()
            : SRange(0,0) {};
    };
    struct SStorageRanges
    {
        std::string device;
        std::vector<SRange> ranges;

        SStorageRanges() {};
    };
}
