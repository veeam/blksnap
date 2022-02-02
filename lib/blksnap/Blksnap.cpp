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
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <system_error>
#include <blksnap/Blksnap.h>

static const char* blksnap_filename = "/dev/" BLK_SNAP_MODULE_NAME;

using namespace blksnap;

CBlksnap::CBlksnap()
    : m_fd(0)
{
    int fd = ::open(blksnap_filename, O_RDWR);
    if (fd < 0)
        throw std::system_error(errno, std::generic_category(), blksnap_filename);

    m_fd = fd;
}

CBlksnap::~CBlksnap()
{
    if (m_fd)
        ::close(m_fd);
}

void CBlksnap::Version(struct blk_snap_version &version)
{
    if (::ioctl(m_fd, IOCTL_BLK_SNAP_VERSION, &version))
        throw std::system_error(errno, std::generic_category(), "Failed to get version.");
}

#ifdef BLK_SNAP_MODIFICATION
bool CBlksnap::Modification(struct blk_snap_mod &mod)
{
    if (::ioctl(m_fd, IOCTL_BLK_SNAP_MOD, &mod)) {
        if (errno == -ENOTTY)
            return false;
        throw std::system_error(errno, std::generic_category(), "Failed to get modification.");
    }
    return true;
}
#endif

void CBlksnap::CollectTrackers(std::vector<struct blk_snap_cbt_info> &cbtInfoVector)
{
    struct blk_snap_tracker_collect param = {0};

    if (::ioctl(m_fd, IOCTL_BLK_SNAP_TRACKER_COLLECT, &param))
        throw std::system_error(errno, std::generic_category(),
            "[TBD]Failed to collect block devices with change tracking.");

    cbtInfoVector.resize(param.count);
    param.cbt_info_array = cbtInfoVector.data();

    if (::ioctl(m_fd, IOCTL_BLK_SNAP_TRACKER_COLLECT, &param))
        throw std::system_error(errno, std::generic_category(),
            "[TBD]Failed to collect block devices with change tracking.");
}

void CBlksnap::ReadCbtMap(struct blk_snap_dev_t dev_id,
                          unsigned int offset, unsigned int length, uint8_t *buff)
{
    struct blk_snap_tracker_read_cbt_bitmap param = {
        .dev_id = dev_id,
        .offset = offset,
        .length = length,
        .buff = buff
    };

    int ret = ::ioctl(m_fd, IOCTL_BLK_SNAP_TRACKER_READ_CBT_MAP, &param);
    if (ret < 0)
        throw std::system_error(errno, std::generic_category(),
            "[TBD]Failed to read difference map from change tracking.");
    if (ret != length)
        throw std::runtime_error(
            "[TBD]Cannot read required bytes of difference map from change tracking.");
}

void CBlksnap::Create(const std::vector<struct blk_snap_dev_t> &devices, uuid_t &id)
{
    struct blk_snap_snapshot_create param = {0};

    std::vector<struct blk_snap_dev_t> localDevices = devices;
    param.count = localDevices.size();
    param.dev_id_array = localDevices.data();

    if (::ioctl(m_fd, IOCTL_BLK_SNAP_SNAPSHOT_CREATE, &param))
        throw std::system_error(errno, std::generic_category(),
            "[TBD]Failed to create snapshot object.");

    uuid_copy(id, param.id);
}

void CBlksnap::Destroy(const uuid_t &id)
{
    struct blk_snap_snapshot_destroy param = {0};

    uuid_copy(param.id, id);

    if (::ioctl(m_fd, IOCTL_BLK_SNAP_SNAPSHOT_DESTROY, &param))
        throw std::system_error(errno, std::generic_category(),
            "[TBD]Failed to destroy snapshot.");
}

void CBlksnap::Collect(const uuid_t &id, std::vector<blk_snap_image_info> &images)
{
    struct blk_snap_snapshot_collect_images param = {0};

    uuid_copy(param.id, id);

    if (::ioctl(m_fd, IOCTL_BLK_SNAP_SNAPSHOT_COLLECT_IMAGES, &param))
        throw std::system_error(errno, std::generic_category(),
            "[TBD]Failed to get device collection for snapshot images.");

    if (param.count == 0)
        return;

    images.resize(param.count);
    param.image_info_array = images.data();

    if (::ioctl(m_fd, IOCTL_BLK_SNAP_SNAPSHOT_COLLECT_IMAGES, &param))
        throw std::system_error(errno, std::generic_category(),
            "[TBD]Failed to get device collection for snapshot images.");

}

void CBlksnap::AppendDiffStorage(const uuid_t &id, const struct blk_snap_dev_t &dev_id,
                                 const std::vector<struct blk_snap_block_range> &ranges)
{
    struct blk_snap_snapshot_append_storage param = {0};

    uuid_copy(param.id, id);
    param.dev_id = dev_id;
    std::vector<struct blk_snap_block_range> localRanges = ranges;
    param.count = localRanges.size();
    param.ranges = localRanges.data();

    if (::ioctl(m_fd, IOCTL_BLK_SNAP_SNAPSHOT_APPEND_STORAGE, &param))
        throw std::system_error(errno, std::generic_category(),
            "[TBD]Failed to append storage for snapshot.");
}

void CBlksnap::Take(const uuid_t &id)
{
    struct blk_snap_snapshot_take param;

    uuid_copy(param.id, id);

    if (::ioctl(m_fd, IOCTL_BLK_SNAP_SNAPSHOT_TAKE, &param))
        throw std::system_error(errno, std::generic_category(),
            "[TBD]Failed to take snapshot.");
}

bool CBlksnap::WaitEvent(const uuid_t &id, unsigned int timeoutMs, SBlksnapEvent &ev)
{
    struct blk_snap_snapshot_event param;

    uuid_copy(param.id, id);
    param.timeout_ms = timeoutMs;

    if (::ioctl(m_fd, IOCTL_BLK_SNAP_SNAPSHOT_WAIT_EVENT, &param)) {
        if ((errno == ENOENT) || (errno == EINTR))
            return false;
        else
            throw std::system_error(errno, std::generic_category(),
                "[TBD]Failed to get event from snapshot.");
    }
    ev.code = param.code;
    ev.time = param.time_label;

    switch (param.code) {
        case blk_snap_event_code_low_free_space:
        {
            struct blk_snap_event_low_free_space *lowFreeSpace =
                (struct blk_snap_event_low_free_space*)(param.data);

            ev.lowFreeSpace.requestedSectors = lowFreeSpace->requested_nr_sect;
            break;
        }
        case blk_snap_event_code_corrupted:
        {
            struct blk_snap_event_corrupted *corrupted =
                (struct blk_snap_event_corrupted*)(param.data);

            ev.corrupted.origDevId = corrupted->orig_dev_id;
            ev.corrupted.errorCode = corrupted->err_code;
            break;
        }
    }
    return true;
}

#if defined(BLK_SNAP_MODIFICATION) && defined(BLK_SNAP_DEBUG_SECTOR_STATE)
void CBlksnap::GetSectorState(struct blk_snap_dev_t image_dev_id, off_t offset,
                              struct blk_snap_sector_state &state)
{
    struct blk_snap_get_sector_state param = {
        .image_dev_id = image_dev_id,
        .sector = static_cast<__u64>(offset >> SECTOR_SHIFT),
        .state = {0}
    };

    if (::ioctl(m_fd, IOCTL_BLK_SNAP_GET_SECTOR_STATE, &param))
        throw std::system_error(errno, std::generic_category(),
            "[TBD]Failed to get sectors state.");

    state = param.state;
}
#endif
