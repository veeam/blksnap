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
        SCbtInfo(){};
        SCbtInfo(const unsigned int inOriginalMajor, const unsigned int inOriginalMinor, const uint32_t inBlockSize,
                 const uint32_t inBlockCount, const uint64_t inDeviceCapacity, const uuid_t& inGenerationId,
                 const uint8_t inSnapNumber)
            : originalMajor(inOriginalMajor)
            , originalMinor(inOriginalMinor)
            , blockSize(inBlockSize)
            , blockCount(inBlockCount)
            , deviceCapacity(inDeviceCapacity)
            , snapNumber(inSnapNumber)
        {
            uuid_copy(generationId, inGenerationId);
        };
        ~SCbtInfo(){};

        unsigned int originalMajor;
        unsigned int originalMinor;
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
        virtual ~ICbt(){};

        virtual std::shared_ptr<SCbtInfo> GetCbtInfo(const std::string& original) = 0;
        virtual std::shared_ptr<SCbtData> GetCbtData(const std::shared_ptr<SCbtInfo>& ptrCbtInfo) = 0;

        static std::shared_ptr<ICbt> Create();
    };

}
