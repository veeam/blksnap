#include "BlkSnapStoreCtx.h"

#include <cstring>
#include <iostream>
#include <string>
#include <sys/sysmacros.h>
#include <system_error>
#include <uuid/uuid.h>

namespace
{
    struct ioctl_dev_id_s to_dev_id(dev_t dev)
    {
        struct ioctl_dev_id_s result;
        result.major = major(dev);
        result.minor = minor(dev);

        return result;
    }
}

BlkSnapStoreCtx::BlkSnapStoreCtx(BlkSnapCtx::Ptr ptrSnapCtx)
    : m_ptrSnapCtx(std::move(ptrSnapCtx))
{}

BlkSnapStoreCtx::BlkSnapStoreCtx(BlkSnapStoreCtx&& other) noexcept
    : m_ptrSnapCtx(std::move(other.m_ptrSnapCtx))
{
    ::memcpy(m_store.id, other.m_store.id, SNAP_ID_LENGTH);
}

BlkSnapStoreCtx::~BlkSnapStoreCtx()
{
    try
    {
        Release();
    }
    catch ( std::exception& ex )
    {
        std::cerr << "Failed to destroy snap store" << std::endl;
    }
}

void BlkSnapStoreCtx::Detach()
{
    static_assert(sizeof(snap_store::id) == sizeof(uuid_t), "[TBD] Check id length");
    uuid_clear(m_store.id);
    //    memset(m_store.id, 0, SNAP_ID_LENGTH);
}

void BlkSnapStoreCtx::Release()
{
    static_assert(sizeof(snap_store::id) == sizeof(uuid_t), "[TBD] Check id length");
    if ( !uuid_is_null(m_store.id) )
        return;

    if ( snap_snapshot_store_cleanup(m_ptrSnapCtx->raw(), &m_store) )
        throw std::system_error(errno, std::generic_category(), "Failed to cleanup snap store");

    uuid_clear(m_store.id);
}

Uuid BlkSnapStoreCtx::GetUuid() const
{
    static_assert(sizeof(snap_store::id) == sizeof(uuid_t), "[TBD] Check id length");
    return Uuid::FromBuffer(m_store.id);
}

BlkSnapStoreCtx BlkSnapStoreCtx::Attach(BlkSnapCtx::Ptr ptrSnapCtx, Uuid uuid)
{
    static_assert(sizeof(snap_store::id) == sizeof(uuid_t), "[TBD] Check id length");
    BlkSnapStoreCtx ctx(std::move(ptrSnapCtx));

    uuid_copy(ctx.m_store.id, uuid.GetRaw());
    return ctx;
}

BlkSnapStoreCtx BlkSnapStoreCtx::Create(BlkSnapCtx::Ptr ptrSnapCtx, dev_t snap_store_dev, std::vector<dev_t> snap_devs)
{
    //@todo implement this
    if ( snap_devs.size() > 1 )
        throw std::runtime_error("Only single device supported");

    struct ioctl_dev_id_s snapStoreDevId = to_dev_id(snap_store_dev);
    struct ioctl_dev_id_s snapDevId = to_dev_id(snap_devs[0]);

    struct snap_store* snapStoreCtx = snap_create_snapshot_store(ptrSnapCtx->raw(), snapStoreDevId, snapDevId);
    if ( snapStoreCtx == nullptr )
        throw std::system_error(errno, std::generic_category(), "Failed to create snapshot store");

    Uuid uuid = Uuid::FromBuffer(snapStoreCtx->id);
    free(snapStoreCtx);

    return Attach(std::move(ptrSnapCtx), uuid);
}

BlkSnapStoreCtx BlkSnapStoreCtx::CreateInMemory(BlkSnapCtx::Ptr ptrSnapCtx, size_t size, std::vector<dev_t> snap_devs)
{
    BlkSnapStoreCtx storeCtx = BlkSnapStoreCtx::Create(ptrSnapCtx, makedev(0, 0), snap_devs);

    if ( snap_create_inmemory_snapshot_store(ptrSnapCtx->raw(), storeCtx.Raw(), size) != 0 )
        throw std::system_error(errno, std::generic_category(), "Failed to create inmemory snapshot store");

    return storeCtx;
}

snap_store* BlkSnapStoreCtx::Raw()
{
    return &m_store;
}
