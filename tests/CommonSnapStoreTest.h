#pragma once
#include <blk-snap-cpp/BlkSnapStoreCtx.h>
#include <boost/filesystem.hpp>

void CommonSnapStoreTest(boost::filesystem::path testDir, boost::filesystem::path originalDev,
                         BlkSnapStoreCtx& storeCtx);
