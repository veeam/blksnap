#include <iostream>
#include <system_error>
#include <blk-snap/snapshot_ctl.h>
#include <sys/stat.h>
#include <iomanip>
#include "helper.h"

int main(int argc, char *argv[])
{
    if (argc < 3)
        throw std::runtime_error("need path");

    struct snap_ctx* snapCtx;
    if (snap_ctx_create(&snapCtx) != 0)
        throw std::system_error(errno, std::generic_category(), "Failed to create snap context");

    struct stat snapDev;
    stat(argv[1], &snapDev);
    struct ioctl_dev_id_s snapDevId = to_dev_id(snapDev.st_rdev);

    struct stat snapStoreDev;
    stat(argv[2], &snapStoreDev);
    struct ioctl_dev_id_s snapStoreDevId = to_dev_id(snapStoreDev.st_rdev);


    struct snap_store_ctx* snapStoreCtx = snap_create_snapshot_store(snapCtx, snapStoreDevId, snapDevId);

    if (snapStoreCtx == nullptr)
        throw std::system_error(errno, std::generic_category(), "Failed to create snapshot store");

    snap_store_ctx_free(snapStoreCtx);
    snap_ctx_destroy(snapCtx);
    std::cout << "Successfully create snapshot store" << std::endl;
}



