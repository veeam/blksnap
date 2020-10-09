#ifndef BLK_SNAP_BLKSNAPSTORECTX_H
#define BLK_SNAP_BLKSNAPSTORECTX_H

#include <blk-snap/snapshot_ctl.h>
#include <boost/uuid/uuid.hpp>
#include <sys/stat.h>
#include <vector>

#include "BlkSnapCtx.h"
#include "Uuid.h"

class BlkSnapStoreCtx
{
public:
    BlkSnapStoreCtx(BlkSnapStoreCtx&& other) noexcept;

    BlkSnapStoreCtx(const BlkSnapStoreCtx&) = delete;
    BlkSnapStoreCtx& operator=(const BlkSnapStoreCtx&) = delete;

    ~BlkSnapStoreCtx();

    void Detach();
    void Release();
    static BlkSnapStoreCtx Attach(BlkSnapCtx::Ptr ptrSnapCtx, Uuid uuid);

    Uuid GetUuid() const;

    static BlkSnapStoreCtx CreateInMemory(BlkSnapCtx::Ptr ptrSnapCtx, size_t size, std::vector<dev_t> snap_devs);

    snap_store* Raw();
    BlkSnapCtx::Ptr GetBlkSnapCtx();
    
    static BlkSnapStoreCtx Create(BlkSnapCtx::Ptr ptrSnapCtx, dev_t snap_store_dev, std::vector<dev_t> snap_devs);
    
private:
    BlkSnapStoreCtx(BlkSnapCtx::Ptr ptrSnapCtx);

private:
    BlkSnapCtx::Ptr m_ptrSnapCtx;
    snap_store m_store;
};

#endif // BLK_SNAP_BLKSNAPSTORECTX_H
