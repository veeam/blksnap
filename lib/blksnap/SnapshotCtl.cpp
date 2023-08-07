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
#include <blksnap/SnapshotCtl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <system_error>
#include <memory>

static const char* blksnap_filename = "/dev/" BLKSNAP_CTL;

using namespace blksnap;

OpenFileHolder::OpenFileHolder(const std::string& filename, int flags)
{
    int fd = ::open(filename.c_str(), flags);
    if (fd < 0)
        throw std::system_error(errno, std::generic_category(), "Cannot open file.");
    m_fd = fd;
};
OpenFileHolder::~OpenFileHolder()
{
    ::close(m_fd);
};

int OpenFileHolder::Get()
{
    return m_fd;
};


CSnapshotCtl::CSnapshotCtl()
    : m_fd(0)
{
    int fd = ::open(blksnap_filename, O_RDWR);
    if (fd < 0)
        throw std::system_error(errno, std::generic_category(), blksnap_filename);

    m_fd = fd;
}

CSnapshotCtl::~CSnapshotCtl()
{
    if (m_fd)
        ::close(m_fd);
}

void CSnapshotCtl::Version(struct blksnap_version& version)
{
    if (::ioctl(m_fd, IOCTL_BLKSNAP_VERSION, &version))
        throw std::system_error(errno, std::generic_category(),
            "Failed to get version.");
}

CSnapshotId CSnapshotCtl::Create(const std::string& filePath, const unsigned long long limit)
{
    OpenFileHolder fd(filePath, O_RDWR);

    struct blksnap_snapshot_create param = {0};
    param.diff_storage_limit_sect = limit / 512;
    param.diff_storage_fd = fd.Get();
    if (::ioctl(m_fd, IOCTL_BLKSNAP_SNAPSHOT_CREATE, &param))
        throw std::system_error(errno, std::generic_category(),
            "Failed to create snapshot object.");

    return CSnapshotId(param.id.b);
}

void CSnapshotCtl::Destroy(const CSnapshotId& id)
{
    struct blksnap_uuid param;

    uuid_copy(param.b, id.Get());

    if (::ioctl(m_fd, IOCTL_BLKSNAP_SNAPSHOT_DESTROY, &param))
        throw std::system_error(errno, std::generic_category(),
            "Failed to destroy snapshot.");
}

void CSnapshotCtl::Collect(std::vector<CSnapshotId>& ids)
{
    struct blksnap_snapshot_collect param = {0};

    ids.clear();
    if (::ioctl(m_fd, IOCTL_BLKSNAP_SNAPSHOT_COLLECT, &param))
        throw std::system_error(errno, std::generic_category(),
            "Failed to get list of active snapshots.");

    if (param.count == 0)
        return;

    std::vector<struct blksnap_uuid> id_array(param.count);
    param.ids = (__u64)id_array.data();

    if (::ioctl(m_fd, IOCTL_BLKSNAP_SNAPSHOT_COLLECT, &param))
        throw std::system_error(errno, std::generic_category(),
            "Failed to get list of snapshots.");

    for (size_t inx = 0; inx < param.count; inx++)
        ids.emplace_back(id_array[inx].b);
}

void CSnapshotCtl::Take(const CSnapshotId& id)
{
    struct blksnap_uuid param;

    uuid_copy(param.b, id.Get());

    if (::ioctl(m_fd, IOCTL_BLKSNAP_SNAPSHOT_TAKE, &param))
        throw std::system_error(errno, std::generic_category(),
            "Failed to take snapshot.");
}

bool CSnapshotCtl::WaitEvent(const CSnapshotId& id, unsigned int timeoutMs, SBlksnapEvent& ev)
{
    struct blksnap_snapshot_event param;

    uuid_copy(param.id.b, id.Get());
    param.timeout_ms = timeoutMs;

    if (::ioctl(m_fd, IOCTL_BLKSNAP_SNAPSHOT_WAIT_EVENT, &param))
    {
        if ((errno == ENOENT) || (errno == EINTR))
            return false;

        throw std::system_error(errno, std::generic_category(), "Failed to get event from snapshot.");
    }
    ev.code = param.code;
    ev.time = param.time_label;

    switch (param.code)
    {
    case blksnap_event_code_corrupted:
    {
        struct blksnap_event_corrupted* corrupted = (struct blksnap_event_corrupted*)(param.data);

        ev.corrupted.origDevIdMj = corrupted->dev_id_mj;
        ev.corrupted.origDevIdMn = corrupted->dev_id_mn;
        ev.corrupted.errorCode = corrupted->err_code;
        break;
    }
    }
    return true;
}
