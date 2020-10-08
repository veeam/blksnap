#include "../include/blk-snap-cpp/Snapshot.h"

#include <iostream>
#include <system_error>

#include "Helper.h"

Snapshot::Snapshot(BlkSnapCtx::Ptr ptrBlkSnap, unsigned long long snapshotId)
    : m_snapshotId(snapshotId)
    , m_ptrBlkSnap(std::move(ptrBlkSnap))
{}

Snapshot::Snapshot(Snapshot&& other) noexcept
    : m_snapshotId(other.m_snapshotId)
    , m_ptrBlkSnap(std::move(other.m_ptrBlkSnap))
{}

Snapshot::~Snapshot()
{
    try
    {
        Release();
    }
    catch ( std::exception& ex )
    {
        std::cerr << "Failed to release snapshot" << ex.what() << std::endl;
    }
}

Snapshot Snapshot::Create(BlkSnapStoreCtx& storeCtx, dev_t device)
{
    unsigned long long snapshotId = snap_create_snapshot(storeCtx.GetBlkSnapCtx()->raw(), Helper::ToDevId(device));
    if (snapshotId == 0)
        throw std::system_error(errno, std::generic_category(), "Failed to create snapshot");

    return Snapshot(storeCtx.GetBlkSnapCtx(), snapshotId);
}

void Snapshot::Release()
{
    if (m_snapshotId == 0)
        return;

    if (snap_destroy_snapshot(m_ptrBlkSnap->raw(), m_snapshotId))
        throw std::system_error(errno, std::generic_category(), "Failed to destroy snapshot");

    m_snapshotId = 0;
}

Snapshot Snapshot::Attach(BlkSnapCtx::Ptr ptrBlkSnap, unsigned long long snapshotId)
{
    return Snapshot(ptrBlkSnap, snapshotId);
}

void Snapshot::Detach()
{
    m_snapshotId = 0;
}

unsigned long long Snapshot::GetId() const noexcept
{
    return m_snapshotId;
}
