/*
 * The low-level abstraction over ioctl for the blksnap kernel module.
 * Allows to interact with the module with minimal overhead and maximum
 * flexibility. Uses structures that are directly passed to the kernel module.
 */

#include <vector>
#include <string>
#include <uuid/uuid.h>
#include "../../../module/blk_snap.h"

#ifndef SECTOR_SHIFT
#define SECTOR_SHIFT 9
#endif
#ifndef SECTOR_SIZE
#define SECTOR_SIZE (1 << SECTOR_SHIFT)
#endif

namespace blksnap {

struct SBlksnapEventLowFreeSpace
{
    unsigned long long requestedSectors;
};

struct SBlksnapEventCorrupted
{
    struct blk_snap_dev_t origDevId;
    int errorCode;
};

struct SBlksnapEvent
{
    unsigned int code;
    long long time;
    union {
        SBlksnapEventLowFreeSpace lowFreeSpace;
        SBlksnapEventCorrupted corrupted;
    };
};

class CBlksnap
{
public:
    CBlksnap();
    ~CBlksnap();

    void Version(struct blk_snap_version &version);
    void CollectTrackers(std::vector<struct blk_snap_cbt_info> &cbtInfoVector);
    void ReadCbtMap(struct blk_snap_dev_t dev_id,
                    unsigned int offset, unsigned int length, uint8_t *buff);

    void Create(const std::vector<struct blk_snap_dev_t> &devices, uuid_t &id);
    void Destroy(const uuid_t &id);
    void Collect(const uuid_t &id, std::vector<struct blk_snap_image_info> &images);
    void AppendDiffStorage(const uuid_t &id, const struct blk_snap_dev_t &dev_id,
                           const std::vector<struct blk_snap_block_range> &ranges);
    void Take(const uuid_t &id);
    bool WaitEvent(const uuid_t &id, unsigned int timeoutMs, SBlksnapEvent &ev);
private:
    int m_fd;

};

}
