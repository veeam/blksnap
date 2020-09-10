#include <iostream>
#include <system_error>
#include <blk-snap/snapshot_ctl.h>
#include <blk-snap/stretch_snapshot_ctrl.h>
#include <vector>
#include <cstring>
#include "helper.h"

struct my_client_data
{
    std::string dirPath;
    size_t currentPortion = 0;
};


struct snap_ranges_space* alloc_new_space(my_client_data& clientData)
{

        std::string snapStorePath = clientData.dirPath + "/#" + std::to_string(clientData.currentPortion++);
        std::cout << "Create storage: " << snapStorePath << std::endl;
        int fd = open(snapStorePath.c_str(), O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
        if (fd == -1)
            throw std::system_error(errno, std::generic_category(), "Failed to create storage");

        if (fallocate64(fd, 0, 0, 500 * 1024 * 1024))
            throw std::system_error(errno, std::generic_category(), "Failed to allocate storage space");

        std::vector<struct ioctl_range_s> ranges = EnumRanges(fd);
        size_t structSize = sizeof(struct snap_ranges_space) + (sizeof(struct ioctl_range_s) * ranges.size());
        snap_ranges_space* space = (struct snap_ranges_space*)malloc(structSize);
        space->count = ranges.size();
        for (size_t i = 0; i < ranges.size(); i++)
        {
            space->ranges[i].right = ranges[i].right;
            space->ranges[i].left = ranges[i].left;
        }

        return space;
}

struct snap_ranges_space* alloc_new_space_safe(my_client_data &clientData)
{
    try
    {
        return alloc_new_space(clientData);
    }
    catch (std::exception &ex)
    {
        std::cerr << "Failed to alloc new space" << std::endl;
        std::cerr << ex.what() << std::endl;
        return nullptr;
    }
}


void print_stretch_error(snap_stretch_store_ctx* stretch_ctx)
{
    std::cerr << "Errno: " << stretch_store_ctx_get_errno(stretch_ctx) << std::endl;

    const char* error_str = stretch_store_ctx_get_error_str(stretch_ctx);
    if (error_str)
        std::cerr << "Failed to init stretch store: " << error_str << std::endl;
    else
        std::cerr << "No error string provided" << std::endl;

}

snap_stretch_store_ctx* PrepareStretchCtx(my_client_data& clientData, ioctl_dev_id_s snapDevId, ioctl_dev_id_s snapStoreDevId)
{
    snap_stretch_store_ctx* stretch_ctx;
    stretch_store_ctx_create(&stretch_ctx);
    if (!stretch_ctx)
        throw std::system_error(errno, std::generic_category(), "Failed to create stretch snap context");

    int res = stretch_store_ctx_init(stretch_ctx, 200 * 1024 * 1024 /*200 Mb*/, &snapStoreDevId, &snapDevId, 1);
    if (res != 0)
    {
        print_stretch_error(stretch_ctx);
        throw std::system_error(stretch_store_ctx_get_errno(stretch_ctx), std::generic_category(), "Failed to init stretch store");
    }

    stretch_store_ctx_set_user_data(stretch_ctx, &clientData);
    std::cout << "Stretch snap store id: " << snap_store_to_str(stretch_store_ctx_get_snap_store(stretch_ctx)) << std::endl;
    std::cout << "Alloc first space for snap store." << std::endl;
    snap_ranges_space* space = alloc_new_space(clientData);
    stretch_store_ctx_add_first_space(stretch_ctx, space);
    free(space);

    return stretch_ctx;
};

struct snap_ranges_space* require_snapshot_store_space(struct snap_stretch_store_ctx* stretch_ctx, uint64_t filled)
{
    std::cout << "require_snapshot_store_space" << std::endl;
    std::cout << "filled: " << filled << std::endl;
    my_client_data* clientData = (my_client_data*)stretch_store_ctx_get_user_data(stretch_ctx);
    return alloc_new_space_safe(*clientData);
}

void space_added(struct snap_stretch_store_ctx* stretch_ctx, struct snap_ranges_space* space, int result)
{
    std::cout << "space_added" << std::endl;
    std::cout << "result " << result << std::endl;

    if (space != nullptr)
        free(space);

    if (result != 0)
        print_stretch_error(stretch_ctx);
}

void snapshot_overflow(struct snap_stretch_store_ctx* stretch_ctx, uint64_t filledStatus)
{
    std::cout << "snapshot_overflow" << std::endl;
    std::cout << "filledStatus: " << filledStatus << std::endl;

    print_stretch_error(stretch_ctx);
}

void snapshot_terminated(struct snap_stretch_store_ctx* stretch_ctx, uint64_t filledStatus)
{
    std::cout << "snapshot_terminated" << std::endl;
    std::cout << "filledStatus: " << filledStatus << std::endl;

    print_stretch_error(stretch_ctx);
}

int main(int argc, char *argv[])
{
    struct my_client_data clientData;

    if (argc < 3)
        throw std::runtime_error("need file path");

    struct snap_ctx* snapCtx;
    if (snap_ctx_create(&snapCtx) != 0)
        throw std::system_error(errno, std::generic_category(), "Failed to create snap context");

    struct stat snapDev;
    stat(argv[1], &snapDev);
    struct ioctl_dev_id_s snapDevId = to_dev_id(snapDev.st_rdev);

    clientData.dirPath = std::string(argv[2]);
    struct stat snapStoreDev;
    stat(argv[2], &snapStoreDev);
    struct ioctl_dev_id_s snapStoreDevId = to_dev_id(snapStoreDev.st_dev);

    snap_stretch_store_ctx* stretch_ctx = PrepareStretchCtx(clientData, snapDevId, snapStoreDevId);
    std::cout << "Stretch prepared." << std::endl;

    stretch_callbacks callbacks;
    callbacks.snapshot_terminated = snapshot_terminated;
    callbacks.snapshot_overflow = snapshot_overflow;
    callbacks.snap_space_added_result = space_added;
    callbacks.require_snapstore_space = require_snapshot_store_space;

    /*unsigned long long snapshotId = snap_create_snapshot(snapCtx, snapDevId);
    if (snapshotId == 0)
        throw std::system_error(errno, std::generic_category(), "Failed to create snapshot");

    std::cout << "Successfully create stretch snapshot: " << snapshotId << "." << std::endl;
*/

    std::cout << "Running maintenance loop" << std::endl;
    int res = stretch_store_maintenance_loop(stretch_ctx, &callbacks);

    std::cout << "Exit: " << res << std::endl;
    return res;
}



