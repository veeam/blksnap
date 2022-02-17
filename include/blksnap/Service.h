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
 * Allows to show module kernel version.
 */
#include <string>

namespace blksnap
{
    std::string Version();

    struct SectorState
    {
        uint8_t snapNumberPrevious;
        uint8_t snapNumberCurrent;
        unsigned int chunkState;
    };

    void GetSectorState(const std::string& image, off_t offset, SectorState& state);

}
