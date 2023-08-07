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
        virtual ~ISession() = default;

        virtual bool GetError(std::string& errorMessage) = 0;

        static std::shared_ptr<ISession> Create(
            const std::vector<std::string>& devices,
            const std::string& diffStorageFilePath,
            const unsigned long long limit);
    };

}
