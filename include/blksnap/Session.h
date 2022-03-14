/*
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
 * Allows to create snapshot session.
 */
#include <memory>
#include <string>
#include <vector>
#include "Sector.h"

namespace blksnap
{
    struct ISession
    {
        virtual ~ISession(){};

        virtual std::string GetImageDevice(const std::string& original) = 0;
        virtual std::string GetOriginalDevice(const std::string& image) = 0;
        virtual bool GetError(std::string& errorMessage) = 0;

        // TODO: add limits
        static std::shared_ptr<ISession> Create(const std::vector<std::string>& devices,
                                                const std::string& diffStorage);
        static std::shared_ptr<ISession> Create(const std::vector<std::string>& devices,
                                                const SStorageRanges& diffStorageRanges);
    };

}
