#include <iostream>
#include <system_error>
#include <blksnap/snapshot_ctl.h>
#include <sys/stat.h>

int main(int argc, char *argv[])
{
    if (argc != 2)
        throw std::runtime_error("need path");

    struct snap_ctx* snapCtx;
    if (snap_ctx_create(&snapCtx) != 0)
        throw std::system_error(errno, std::generic_category(), "Failed to create snap context");

    struct stat dev_stat;
    stat(argv[1], &dev_stat);
    if (snap_add_to_tracking(snapCtx, dev_stat.st_rdev) != 0)
        throw std::system_error(errno, std::generic_category(), "Failed to add tracking");

    std::cout << "Successfully add devide(" << argv[1] << ") to tracking" << std::endl;
}


