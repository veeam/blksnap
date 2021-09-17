#ifndef BLK_SNAP_SNAPSHOT_H
#define BLK_SNAP_SNAPSHOT_H

#include "Uuid.h"
#include "BlkSnapCtx.h"
#include "BlkSnapStoreCtx.h"

class Snapshot
{
public:
    Snapshot (const Snapshot& other) = delete;
    Snapshot (Snapshot&& other) noexcept;

    Snapshot& operator= (const Snapshot& other) = delete;

    ~Snapshot();

    static Snapshot Create(BlkSnapStoreCtx& storeCtx, dev_t device);
    static Snapshot Attach(BlkSnapCtx::Ptr ptrBlkSnap, unsigned long long snapshotId);

    void Release();
    void Detach();

    unsigned long long GetId() const noexcept;

private:
    Snapshot(BlkSnapCtx::Ptr ptrBlkSnap , unsigned long long snapshotId);

private:
    unsigned long long m_snapshotId;
    BlkSnapCtx::Ptr m_ptrBlkSnap;
};

#endif // BLK_SNAP_SNAPSHOT_H
