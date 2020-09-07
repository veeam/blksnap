#include <iostream>
#include <system_error>
#include <blk-snap/snapshot_ctl.h>
#include <sys/stat.h>

int main(int argc, char *argv[])
{
    if (argc != 2)
        throw std::runtime_error("need snapshot id");

    struct snap_ctx* snapCtx;
    if (snap_ctx_create(&snapCtx) != 0)
        throw std::system_error(errno, std::generic_category(), "Failed to create snap context");

    unsigned long long snapshotId = std::stoull(argv[1]);
    if (snap_destroy_snapshot(snapCtx, snapshotId))
        throw std::system_error(errno, std::generic_category(), "Failed to destroy snapshot");

    snap_ctx_destroy(snapCtx);
    std::cout << "Successfully destroy snapshot: " << snapshotId << std::endl;
}


