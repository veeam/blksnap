#include <iostream>
#include <system_error>
#include <blk-snap/snapshot_ctl.h>
#include <sys/stat.h>
#include <cstring>
#include "helper.h"

int main(int argc, char *argv[])
{
	if (argc != 2)
		throw std::runtime_error("need path");

	struct snap_ctx* snapCtx;
	if (snap_ctx_create(&snapCtx) != 0)
		throw std::system_error(errno, std::generic_category(), "Failed to create snap context");

	struct stat dev_stat;
	stat(argv[1], &dev_stat);
	struct ioctl_dev_id_s snapDevId = to_dev_id(dev_stat.st_rdev);

	int error = snap_snapshot_get_errno(snapCtx, snapDevId);
	if (error == -1)
		throw std::system_error(errno, std::generic_category(), "Failed to get snapshot errno");

	snap_ctx_destroy(snapCtx);
	std::cout << "Snapshot errno: " << error << "("<< strerror(error) << ")" << std::endl;
}


