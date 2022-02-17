/* [TBD]
 * Copyright (C) 2022 Veeam Software Group GmbH <https://www.veeam.com/contacts.html>
 *
 * This file is part of blksnap-tests
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "SnapshotHelper.h"

#include <blk-snap-cpp/Helper.h>
#include <blk-snap/snapshot_ctl.h>
#include <iostream>
#include <sys/sysmacros.h>

int SnapshotHelper::GetSnapshotDevice(BlkSnapCtx::Ptr ptrCtx, boost::filesystem::path originalDevice)
{
    dev_t dev = Helper::GetDevice(originalDevice.string());

    size_t snapSize = 0;
    if (snap_collect_snapshot_images(ptrCtx->raw(), nullptr, &snapSize) != 0)
    {
        if (errno != ENODATA)
            throw std::system_error(errno, std::generic_category(), "Failed to get snapshot images count");
    }

    image_info_s images_info[snapSize];
    if (snap_collect_snapshot_images(ptrCtx->raw(), images_info, &snapSize) != 0)
    {
        if (errno != ENODATA)
            throw std::system_error(errno, std::generic_category(), "Failed to collect snapshot images");
    }

    for (size_t i = 0; i < snapSize; i++)
    {
        if ((images_info[i].original_dev_id.minor == minor(dev))
            && (images_info[i].original_dev_id.major == major(dev)))
            return images_info[i].snapshot_dev_id.minor;
    }

    throw std::runtime_error("Unable to find snapshot");
}
