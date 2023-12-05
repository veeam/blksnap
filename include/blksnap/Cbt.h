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
 * The hi-level abstraction for the blksnap kernel module.
 * Allows to receive data from CBT.
 */
#include <memory>
#include <uuid/uuid.h>
#include <vector>

namespace blksnap
{
    struct SCbtInfo
    {
        SCbtInfo()
        {};
        SCbtInfo(const uint32_t inBlockSize, const uint32_t inBlockCount,
                 const uint64_t inDeviceCapacity, const uuid_t& inGenerationId,
                 const uint8_t inSnapNumber)
            : blockSize(inBlockSize)
            , blockCount(inBlockCount)
            , deviceCapacity(inDeviceCapacity)
            , snapNumber(inSnapNumber)
        {
            uuid_copy(generationId, inGenerationId);
        };
        ~SCbtInfo(){};

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
        ~SCbtData(){};

        std::vector<uint8_t> vec;
    };

    struct ICbt
    {
        virtual ~ICbt() = default;

        virtual std::string GetImage() = 0;
        virtual int GetError() = 0;
        virtual std::shared_ptr<SCbtInfo> GetCbtInfo() = 0;
        virtual std::shared_ptr<SCbtData> GetCbtData() = 0;

        static std::shared_ptr<ICbt> Create(const std::string& original);
    };

}
