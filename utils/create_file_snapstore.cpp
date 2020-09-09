#include <iostream>
#include <system_error>
#include <blk-snap/snapshot_ctl.h>
#include <sys/stat.h>
#include <vector>
#include <fcntl.h>
#include "helper.h"

int main(int argc, char *argv[])
{
    if (argc < 3)
        throw std::runtime_error("need file path");

    struct snap_ctx* snapCtx;
    if (snap_ctx_create(&snapCtx) != 0)
        throw std::system_error(errno, std::generic_category(), "Failed to create snap context");

    struct stat snapDev;
    stat(argv[1], &snapDev);
    struct ioctl_dev_id_s snapDevId = to_dev_id(snapDev.st_rdev);

    int fd = open(argv[2], O_RDWR);
    if (fd == -1)
        throw std::system_error(errno, std::generic_category(), "Failed to open file");

    struct stat snapStoreDev;
    fstat(fd, &snapStoreDev);
    struct ioctl_dev_id_s snapStoreDevId = to_dev_id(snapStoreDev.st_dev);
    struct snap_store* snapStoreCtx = snap_create_snapshot_store(snapCtx, snapStoreDevId, snapDevId);

    std::cout << "Successfully create snapshot store: " << snap_store_to_str(snapStoreCtx) << std::endl;
    std::vector<struct ioctl_range_s> ranges = EnumRanges(fd);
    if (snap_create_file_snapshot_store(snapCtx, snapStoreCtx, ranges.data(), ranges.size()))
        throw std::system_error(errno, std::generic_category(), "Failed to create file snapshot store");

    unsigned long long snapshotId = snap_create_snapshot(snapCtx, snapDevId);
    if (snapshotId == 0)
        throw std::system_error(errno, std::generic_category(), "Failed to create snapshot");

    snap_store_ctx_free(snapStoreCtx);
    snap_ctx_destroy(snapCtx);
    std::cout << "Successfully create file snapshot id: " << snapshotId << "." << std::endl;
}



