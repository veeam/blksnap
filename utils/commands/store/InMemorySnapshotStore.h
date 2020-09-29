#ifndef BLK_SNAP_INMEMORYSNAPSHOTSTORE_H
#define BLK_SNAP_INMEMORYSNAPSHOTSTORE_H

#include <cstdlib>
#include <vector>

#include <blk-snap/snapshot_ctl.h>
#include <boost/uuid/uuid.hpp>

class InMemorySnapshotStore
{
public:
    InMemorySnapshotStore(size_t capacity, std::vector<dev_t> devices);
    ~InMemorySnapshotStore();


    void release();
    static InMemorySnapshotStore attach(boost::uuids::uuid uuid);

private:
    boost::uuids::uuid m_uuid;
};

#endif // BLK_SNAP_INMEMORYSNAPSHOTSTORE_H
