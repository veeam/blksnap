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
    
    for (size_t i = 0; i < snapSize ; i++)
    {
        if ((images_info[i].original_dev_id.minor == minor(dev)) && (images_info[i].original_dev_id.major == major(dev)))
            return images_info[i].snapshot_dev_id.minor;
    }
    
    throw std::runtime_error("Unable to find snapshot");
}
