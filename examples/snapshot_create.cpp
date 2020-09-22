#include <iostream>
#include <system_error>
#include <blk-snap/snapshot_ctl.h>
#include <blk-snap/stretch_snapshot_ctrl.h>
#include <vector>
#include <cstring>
#include "helper.h"

int main(int argc, char *argv[])
{
    if (argc < 2)
        throw std::runtime_error("need dev path");

    struct snap_ctx* snapCtx;
    if (snap_ctx_create(&snapCtx) != 0)
        throw std::system_error(errno, std::generic_category(), "Failed to create snap context");

    struct stat snapDev;
    stat(argv[1], &snapDev);
    struct ioctl_dev_id_s snapDevId = to_dev_id(snapDev.st_rdev);

    unsigned long long snapshotId = snap_create_snapshot(snapCtx, snapDevId);
    if (snapshotId == 0)
        throw std::system_error(errno, std::generic_category(), "Failed to create snapshot");

    std::cout << "Successfully create snapshot: " << snapshotId << "." << std::endl;
}



