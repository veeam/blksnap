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
 * Allows to show module kernel version.
 */
#include <string>
#include <vector>
#include "SnapshotId.h"
#include "OpenFileHolder.h"

namespace blksnap
{
    class CService
    {
    public:
        CService();
        ~CService() {};

        void Collect(std::vector<CSnapshotId>& ids);
        void Version(unsigned short& major, unsigned short& minor, unsigned short& revision, unsigned short& build);
        bool GetModification(unsigned long long& flags, std::string& name);
        bool SetLog(const int tz_minuteswest, const int level, const std::string& filepath);

    private:
        COpenFileHolder m_ctl;
    };
}
