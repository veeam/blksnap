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
#include <blksnap/Snapshot.h>
#include <blksnap/Service.h>
#include <linux/blksnap.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <iostream>
#include <sstream>


static const char* blksnap_filename = "/dev/" BLKSNAP_CTL;

using namespace blksnap;

CService::CService()
    : m_ctl(blksnap_filename, O_RDWR)
{ }

void CService::Version(unsigned short& major, unsigned short& minor, unsigned short& revision, unsigned short& build)
{
    struct blksnap_version version = {};

    if (::ioctl(m_ctl.Get(), IOCTL_BLKSNAP_VERSION, &version))
        throw std::system_error(errno, std::generic_category(),
            "Failed to get version.");

    major = version.major;
    minor = version.minor;
    revision = version.revision;
    build = version.build;
}

void CService::Collect(std::vector<CSnapshotId>& ids)
{
    struct blksnap_snapshot_collect param = {0};

    ids.clear();
    if (::ioctl(m_ctl.Get(), IOCTL_BLKSNAP_SNAPSHOT_COLLECT, &param))
        throw std::system_error(errno, std::generic_category(),
            "Failed to get list of active snapshots.");

    if (param.count == 0)
        return;

    std::vector<struct blksnap_uuid> id_array(param.count);
    param.ids = (__u64)id_array.data();

    if (::ioctl(m_ctl.Get(), IOCTL_BLKSNAP_SNAPSHOT_COLLECT, &param))
        throw std::system_error(errno, std::generic_category(),
            "Failed to get list of snapshots.");

    for (size_t inx = 0; inx < param.count; inx++)
        ids.emplace_back(id_array[inx].b);
}
