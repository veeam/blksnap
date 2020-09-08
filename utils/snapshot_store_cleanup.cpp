#include <iostream>
#include <system_error>
#include <blk-snap/snapshot_ctl.h>
#include "helper.h"

int main(int argc, char *argv[])
{
    if (argc != 2)
        throw std::runtime_error("need snapshot store id");

    struct snap_ctx* snapCtx;
    if (snap_ctx_create(&snapCtx) != 0)
        throw std::system_error(errno, std::generic_category(), "Failed to create snap context");

    snap_store store = {0};
    str_to_snap_id(std::string(argv[1]), store.id);
    if (snap_snapshot_store_cleanup(snapCtx, &store))
        throw std::system_error(errno, std::generic_category(), "Failed to cleanup snap store");

    snap_ctx_destroy(snapCtx);
    std::cout << "Successfully cleanup snapshot store: " << argv[1] << std::endl;
}


