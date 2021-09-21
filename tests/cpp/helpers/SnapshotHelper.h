#pragma once

#include <boost/filesystem.hpp>
#include <blk-snap-cpp/BlkSnapCtx.h>

class SnapshotHelper
{
public:
    static int GetSnapshotDevice(BlkSnapCtx::Ptr ptrCtx, boost::filesystem::path originalDevice);
};
