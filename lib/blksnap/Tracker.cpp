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
#include <blksnap/Tracker.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>

using namespace blksnap;

#define BLKSNAP_FILTER_NAME {'b','l','k','s','n','a','p','\0'}

CTracker::CTracker(const std::string& devicePath)
    : m_devicePath(devicePath)
{
    const std::string bdevfilterPath("/dev/" BDEVFILTER);

    m_bdevfilter = ::open(bdevfilterPath.c_str(), O_RDWR);
    if (m_bdevfilter < 0)
        throw std::system_error(errno, std::generic_category(), "Failed to open ["+bdevfilterPath+"] device");
}
CTracker::~CTracker()
{
    if (m_bdevfilter > 0) {
        ::close(m_bdevfilter);
        m_bdevfilter = 0;
    }
}

bool CTracker::Attach()
{
    struct bdevfilter_name name = {
        .devpath = (__u64)m_devicePath.c_str(),
        .name = BLKSNAP_FILTER_NAME,
    };

    if (::ioctl(m_bdevfilter, BDEVFILTER_ATTACH, &name) < 0) {
        if (errno == EALREADY)
            return false;
        else
            throw std::system_error(errno, std::generic_category(),
                "Failed to attach 'blksnap' filter.");
    }
    return true;
}
void CTracker::Detach()
{
    struct bdevfilter_name name = {
        .devpath = (__u64)m_devicePath.c_str(),
        .name = BLKSNAP_FILTER_NAME,
    };

    if (::ioctl(m_bdevfilter, BDEVFILTER_DETACH, &name) < 0)
        throw std::system_error(errno, std::generic_category(),
            "Failed to detach 'blksnap' filter.");

}

void CTracker::CbtInfo(struct blksnap_cbtinfo& cbtInfo)
{
    struct bdevfilter_ctl ctl = {
        .devpath = (__u64)m_devicePath.c_str(),
        .name = BLKSNAP_FILTER_NAME,
        .cmd = BLKFILTER_CTL_BLKSNAP_CBTINFO,
        .optlen = sizeof(cbtInfo),
        .opt = (__u64)&cbtInfo,
    };

    if (::ioctl(m_bdevfilter, BDEVFILTER_CTL, &ctl) < 0)
        throw std::system_error(errno, std::generic_category(),
            "Failed to get CBT information.");
}
void CTracker::ReadCbtMap(unsigned int offset, unsigned int length, uint8_t* buff)
{
    struct blksnap_cbtmap arg = {
        .offset = offset,
        .length = length,
        .buffer = (__u64)buff
    };
    struct bdevfilter_ctl ctl = {
        .devpath = (__u64)m_devicePath.c_str(),
        .name = BLKSNAP_FILTER_NAME,
        .cmd = BLKFILTER_CTL_BLKSNAP_CBTMAP,
        .optlen = sizeof(arg),
        .opt = (__u64)&arg,
    };

    if (::ioctl(m_bdevfilter, BDEVFILTER_CTL, &ctl) < 0)
        throw std::system_error(errno, std::generic_category(),
            "Failed to read CBT map.");

}
void CTracker::MarkDirtyBlock(std::vector<struct blksnap_sectors>& ranges)
{
    struct blksnap_cbtdirty arg = {
        .count = static_cast<unsigned int>(ranges.size()),
        .dirty_sectors = (__u64)ranges.data(),
    };
    struct bdevfilter_ctl ctl = {
        .devpath = (__u64)m_devicePath.c_str(),
        .name = BLKSNAP_FILTER_NAME,
        .cmd = BLKFILTER_CTL_BLKSNAP_CBTDIRTY,
        .optlen = sizeof(arg),
        .opt = (__u64)&arg,
    };

    if (::ioctl(m_bdevfilter, BDEVFILTER_CTL, &ctl) < 0)
        throw std::system_error(errno, std::generic_category(),
            "Failed to mark block as 'dirty' in CBT map.");
}
void CTracker::SnapshotAdd(const uuid_t& id)
{
    struct blksnap_snapshotadd arg;
    uuid_copy(arg.id.b, id);

    struct bdevfilter_ctl ctl = {
        .devpath = (__u64)m_devicePath.c_str(),
        .name = BLKSNAP_FILTER_NAME,
        .cmd = BLKFILTER_CTL_BLKSNAP_SNAPSHOTADD,
        .optlen = sizeof(arg),
        .opt = (__u64)&arg,
    };

    if (::ioctl(m_bdevfilter, BDEVFILTER_CTL, &ctl) < 0)
        throw std::system_error(errno, std::generic_category(),
            "Failed to add device to snapshot.");
}
void CTracker::SnapshotInfo(struct blksnap_snapshotinfo& snapshotinfo)
{
    struct bdevfilter_ctl ctl = {
        .devpath = (__u64)m_devicePath.c_str(),
        .name = BLKSNAP_FILTER_NAME,
        .cmd = BLKFILTER_CTL_BLKSNAP_SNAPSHOTINFO,
        .optlen = sizeof(snapshotinfo),
        .opt = (__u64)&snapshotinfo,
    };

    if (::ioctl(m_bdevfilter, BDEVFILTER_CTL, &ctl) < 0)
        throw std::system_error(errno, std::generic_category(),
            "Failed to get snapshot information.");
}
