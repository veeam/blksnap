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
#include <linux/blksnap.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <system_error>

static const char* blksnap_filename = "/dev/" BLKSNAP_CTL;

using namespace blksnap;

CSnapshot::CSnapshot(const CSnapshotId& id, const std::shared_ptr<COpenFileHolder>& ctl)
    : m_id(id)
    , m_ctl(ctl)
{ }

std::shared_ptr<CSnapshot> CSnapshot::Create(const std::string& filePath, const unsigned long long limit)
{
    if (filePath.empty())
        throw std::runtime_error("The parameter 'filePath' cannot be empty");

    struct blksnap_snapshot_create param = {0};
    param.diff_storage_limit_sect = limit / 512;
    param.diff_storage_filename = (__u64)filePath.c_str();

    auto ctl = std::make_shared<COpenFileHolder>(blksnap_filename, O_RDWR);
    if (::ioctl(ctl->Get(), IOCTL_BLKSNAP_SNAPSHOT_CREATE, &param))
        throw std::system_error(errno, std::generic_category(),
            "Failed to create snapshot object.");

    return std::shared_ptr<CSnapshot>(new
        CSnapshot(CSnapshotId(param.id.b), ctl));
}

std::shared_ptr<CSnapshot> CSnapshot::Open(const CSnapshotId& id)
{
    return std::shared_ptr<CSnapshot>(new
        CSnapshot(id, std::make_shared<COpenFileHolder>(blksnap_filename, O_RDWR)));
}

void CSnapshot::Take()
{
    struct blksnap_uuid param;

    uuid_copy(param.b, m_id.Get());
    if (::ioctl(m_ctl->Get(), IOCTL_BLKSNAP_SNAPSHOT_TAKE, &param))
        throw std::system_error(errno, std::generic_category(),
            "Failed to take snapshot.");
}

void CSnapshot::Destroy()
{
    struct blksnap_uuid param;

    uuid_copy(param.b, m_id.Get());
    if (::ioctl(m_ctl->Get(), IOCTL_BLKSNAP_SNAPSHOT_DESTROY, &param))
        throw std::system_error(errno, std::generic_category(),
            "Failed to destroy snapshot.");
}

bool CSnapshot::WaitEvent(unsigned int timeoutMs, SBlksnapEvent& ev)
{
    struct blksnap_snapshot_event param = {0};

    uuid_copy(param.id.b, m_id.Get());
    param.timeout_ms = timeoutMs;

    if (::ioctl(m_ctl->Get(), IOCTL_BLKSNAP_SNAPSHOT_WAIT_EVENT, &param))
    {
        if ((errno == ENOENT) || (errno == EINTR))
            return false;
        if (errno == ESRCH)
            throw std::system_error(errno, std::generic_category(), "Snapshot not found");

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
    case blksnap_event_code_no_space:
    {
        struct blksnap_event_no_space* data = (struct blksnap_event_no_space*)(param.data);

        ev.noSpace.requestedSectors = data->requested_nr_sect;
        break;
    }
    default:
        throw std::runtime_error("An unsupported event ["+std::to_string(param.code)+"] was received.");
    }
    return true;
}
