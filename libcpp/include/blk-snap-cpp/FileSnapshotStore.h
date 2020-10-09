#pragma once

#include <blk-snap-cpp/BlkSnapCtx.h>
#include <blk-snap-cpp/BlkSnapStoreCtx.h>

class FileSnapshotStore
{
public:
    FileSnapshotStore(BlkSnapCtx::Ptr ptrSnapCtx, std::vector<dev_t> snapDevs, std::string snapStoreFile);
    ~FileSnapshotStore() = default;
    
    const BlkSnapStoreCtx& GetSnapStoreCtx() const;
    BlkSnapStoreCtx& GetSnapStoreCtx();

private:
    void FillSnapStore(std::string snapStoreFile);
    
private:
    BlkSnapStoreCtx m_storeCtx;
};
