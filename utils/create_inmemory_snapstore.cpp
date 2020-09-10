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

	size_t size = std::stoll(argv[2]);

	struct ioctl_dev_id_s snapStoreDevId = {0,0};
	struct snap_store* snapStoreCtx = snap_create_snapshot_store(snapCtx, snapStoreDevId, snapDevId);

	if (snapStoreCtx == nullptr)
		throw std::system_error(errno, std::generic_category(), "Failed to create snapshot store");

	if (snap_create_inmemory_snapshot_store(snapCtx, snapStoreCtx, size) != 0)
		throw std::system_error(errno, std::generic_category(), "Failed to create inmemory snapshot store");

	unsigned long long snapshotId = snap_create_snapshot(snapCtx, snapDevId);
	if (snapshotId == 0)
		throw std::system_error(errno, std::generic_category(), "Failed to create snapshot");

	snap_store_ctx_free(snapStoreCtx);
	snap_ctx_destroy(snapCtx);
	std::cout << "Successfully create in memory snapshot id: " << snapshotId << "." << std::endl;
}



