#include <vector>
#include <map>
#include <cstring>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <uuid/uuid.h>
#include <linux/fs.h>
#include <linux/fiemap.h>

#include "BlksnapHelper.h"
#include "../../../module/blk_snap.h"


static const char* blksnap_filename = "/dev/" BLK_SNAP_MODULE_NAME;

struct SBlksnapInfo
{
    struct blk_snap_dev_t original;
    struct blk_snap_dev_t image;
    std::string originalName;
    std::string imageName;
};

class CBlksnap : public IBlksnap
{
public:
    CBlksnap(const std::vector<std::string>& devices);
    ~CBlksnap() override;

    void AppendDiffStorage(std::string filename) override;
    void Take() override;
    std::string GetImageDevice(const std::string& original) override;
private:
    void Create();
    void ReleaseSafe();
    void CBlksnap::Collect();
private:
    int m_fd;
    uuid_t m_id;
    bool m_collected;
    std::vector<SBlksnapInfo> m_devices;
};

std::shared_ptr<IBlksnap> CreateBlksnap(const std::vector<std::string>& devices)
{
    std::shared_ptr<IBlksnap> blksnap = std::make_shared<CBlksnap>(devices);

    return blksnap;
}

static inline
struct blk_snap_dev_t deviceByName(const std::string &name)
{
    struct stat st;

    if (::stat(name.c_str(), &st))
        throw std::system_error(errno, std::generic_category(), name);

    struct blk_snap_dev_t device = {
        .mj = major(st.st_rdev),
        .mn = minor(st.st_rdev),
    };
    return device;
}

static
void fiemapStorage(const std::string &filename,
                   struct blk_snap_dev_t &dev_id,
                   std::vector<struct blk_snap_block_range> &ranges)
{
    int ret = 0;
    const char* errMessage;
    int fd = -1;
    struct fiemap *map = NULL;
    int extentMax = 500;
    long long fileSize;
    struct stat64 st;

    if (::stat64(filename.c_str(), &st))
        throw std::system_error(errno, std::generic_category(), "Failed to get file size.");

    fileSize = st.st_size;
    dev_id.mj = major(st.st_dev);
    dev_id.mn = minor(st.st_dev);

    fd = ::open(filename.c_str(), O_RDONLY | O_EXCL | O_LARGEFILE);
    if (fd < 0) {
        ret = errno;
        errMessage = "Failed to open file.";
        goto fail;
    }

    map = (struct fiemap *)::malloc(sizeof(struct fiemap) + sizeof(struct fiemap_extent) * extentMax);
    if (!map) {
        ret = ENOMEM;
        errMessage = "Failed to allocate memory for fiemap structure.";
        goto fail;
    }

    for (long long fileOffset = 0; fileOffset < fileSize; ) {

        map->fm_start = fileOffset;
        map->fm_length = fileSize - fileOffset;
        map->fm_extent_count = extentMax;
        map->fm_flags = 0;

        if (::ioctl(fd, FS_IOC_FIEMAP, map)) {
            ret = errno;
            errMessage = "Failed to call FS_IOC_FIEMAP.";
            goto fail;
        }

        for (int i=0; i < map->fm_mapped_extents; ++i) {
            struct blk_snap_block_range rg;
            struct fiemap_extent *extent = map->fm_extents + i;

            if (extent->fe_physical & (SECTOR_SIZE - 1)) {
                ret = EINVAL;
                errMessage = "File location is not ordered by sector size.";
                goto fail;
            }

            rg.sector_offset = extent->fe_physical >> SECTOR_SHIFT;
            rg.sector_count = extent->fe_length >> SECTOR_SHIFT;
            ranges.push_back(rg);

            fileOffset = extent->fe_logical + extent->fe_length;

            //std::cout << "allocate range: ofs=" << rg.sector_offset << " cnt=" << rg.sector_count << std::endl;
        }
    }

    ::close(fd);
    return;

fail:
    if (map)
        ::free(map);
    if (fd >= 0)
        ::close(fd);
    throw std::system_error(ret, std::generic_category(), errMessage);
}

CBlksnap::CBlksnap(const std::vector<std::string>& devices)
    : m_fd(0)
    , m_id({0})
    , m_collected(false)
{
    int fd = ::open(blksnap_filename, O_RDWR);
    if (fd < 0)
        throw std::system_error(errno, std::generic_category(), blksnap_filename);
    m_fd = fd;

    for (const auto& name : devices) {
        SBlksnapInfo info;

        info.originalName = name;
        info.original = deviceByName(name);
        info.image = {0};
        info.imageName = "";
        m_devices.push_back(info);
    }

    Create();
}

CBlksnap::~CBlksnap()
{
    ReleaseSafe();
    if (m_fd)
        ::close(m_fd);
}

void CBlksnap::AppendDiffStorage(std::string filename)
{
    struct blk_snap_snapshot_append_storage param = {0};
    std::vector<struct blk_snap_block_range> ranges;

    uuid_copy(param.id, m_id);
    fiemapStorage(filename, param.dev_id, ranges);
    param.count = ranges.size();
    param.ranges = ranges.data();

    if (::ioctl(m_fd, IOCTL_BLK_SNAP_SNAPSHOT_APPEND_STORAGE, &param))
        throw std::system_error(errno, std::generic_category(),
            "[TBD]Failed to append storage for snapshot.");
}

void CBlksnap::Take()
{
    struct blk_snap_snapshot_take param;

    uuid_copy(param.id, m_id);

    if (::ioctl(blksnap_fd, IOCTL_BLK_SNAP_SNAPSHOT_TAKE, &param))
        throw std::system_error(errno, std::generic_category(),
            "[TBD]Failed to take snapshot.");
}



void CBlksnap::Create()
{
    struct blk_snap_snapshot_create param = {0};
    std::vector<struct blk_snap_dev_t> devices;

    for (const SBlksnapInfo& info : m_devices)
        devices.push_back(info.original)

    param.count = devices.size();
    param.dev_id_array = devices.data();

    if (::ioctl(m_fd, IOCTL_BLK_SNAP_SNAPSHOT_CREATE, &param))
        throw std::system_error(errno, std::generic_category(),
            "[TBD]Failed to create snapshot object.");

    uuid_copy(m_id, param.id);
}

void CBlksnap::ReleaseSafe() noexcept
{
    try
    {
        uuid_copy(param.id, m_id);

        if (::ioctl(m_fd, IOCTL_BLK_SNAP_SNAPSHOT_DESTROY, &param))
            throw std::system_error(errno, std::generic_category(),
                "[TBD]Failed to destroy snapshot.");
    }
    catch(std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
    }
}

void CBlksnap::Collect()
{
    struct blk_snap_snapshot_collect_images param = {0};
    std::vector<struct blk_snap_image_info> imageInfoVector;

    uuid_copy(param.id, m_id);
    if (::ioctl(blksnap_fd, IOCTL_BLK_SNAP_SNAPSHOT_COLLECT_IMAGES, &param))
        throw std::system_error(errno, std::generic_category(),
            "[TBD]Failed to get device collection for snapshot images.");

    if (param.count == 0)
        return;

    imageInfoVector.resize(param.count);
    param.image_info_array = imageInfoVector.data();

    if (::ioctl(blksnap_fd, IOCTL_BLK_SNAP_SNAPSHOT_COLLECT_IMAGES, &param))
        throw std::system_error(errno, std::generic_category(),
            "[TBD]Failed to get device collection for snapshot images.");

    for (const struct blk_snap_image_info& imageInfo : imageInfoVector) {
        for (size_t inx=0; inx<m_devices; inx++) {
            if ((m_devices[inx].original.mj == imageInfo.orig_dev_id.mj) &&
                (m_devices[inx].original.mn == imageInfo.orig_dev_id.mn))
            {
                m_devices[inx].image = imageInfo.image_dev_id;
                m_devices[inx].imageName = "/dev/" + BLK_SNAP_IMAGE_NAME + std::to_string(imageInfo.image_dev_id.mn);
            }
        }
    }

    m_collected = true;
}

std::string CBlksnap::GetImageDevice(const std::string& original)
{
    struct blk_snap_dev_t orig_dev_id = deviceByName(original);

    if (!m_collected)
        Collect();

    for (size_t inx=0; inx<m_devices; inx++) {
        if ((m_devices[inx].original.mj == orig_dev_id.mj) &&
            (m_devices[inx].original.mn == orig_dev_id.mn))
            return m_devices[inx].image;
    }

    throw std::runtime_error("Failed to get image device for '"+original+"'.");
}
