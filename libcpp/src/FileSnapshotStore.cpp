#include <blk-snap-cpp/FileSnapshotStore.h>
#include <blk-snap-cpp/Helper.h>
#include <system_error>

FileSnapshotStore::FileSnapshotStore(BlkSnapCtx::Ptr ptrSnapCtx, std::vector<dev_t> snapDevs, std::string snapStoreFile)
    : m_storeCtx(BlkSnapStoreCtx::Create(ptrSnapCtx, Helper::GetDeviceForFile(snapStoreFile), snapDevs))
{
    FillSnapStore(snapStoreFile);
}

void FileSnapshotStore::FillSnapStore(std::string snapStoreFile)
{
    std::vector<fiemap_extent> extends = Helper::Fiemap(snapStoreFile);

    std::vector<struct ioctl_range_s> ranges;
    ranges.reserve(extends.size());
    for ( auto& ext : extends )
    {
        struct ioctl_range_s st;
        st.left = ext.fe_physical;
        st.right = ext.fe_physical + ext.fe_length;
        ranges.push_back(st);
    }

    if ( snap_create_file_snapshot_store(m_storeCtx.GetBlkSnapCtx()->raw(), m_storeCtx.Raw(), ranges.data(), ranges.size()) )
        throw std::system_error(errno, std::generic_category(), "Failed to create file snapshot store");
}

const BlkSnapStoreCtx& FileSnapshotStore::GetSnapStoreCtx() const
{
    return m_storeCtx;
}

BlkSnapStoreCtx& FileSnapshotStore::GetSnapStoreCtx()
{
    return m_storeCtx;
}
