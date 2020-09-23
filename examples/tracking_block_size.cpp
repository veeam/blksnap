#include <iostream>
#include <system_error>
#include <blk-snap/snapshot_ctl.h>
#include <sys/stat.h>

int main(int argc, char *argv[])
{
	struct snap_ctx* snapCtx;
	if (snap_ctx_create(&snapCtx) != 0)
		throw std::system_error(errno, std::generic_category(), "Failed to create snap context");

	unsigned int blockSize = snap_get_tracking_block_size(snapCtx);
	if (blockSize == 0)
		throw std::system_error(errno, std::generic_category(), "Failed to get tracking block size");

	snap_ctx_destroy(snapCtx);
	std::cout << "Tracking block size: " << blockSize << std::endl;
}


