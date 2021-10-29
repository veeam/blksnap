#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <cstring>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <uuid/uuid.h>
#include <linux/fs.h>
#include <linux/fiemap.h>

#include <boost/program_options.hpp>
namespace po = boost::program_options;
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;

#include "../../module/blk_snap.h"

#ifndef SECTOR_SHIFT
#define SECTOR_SHIFT 9
#endif
#ifndef SECTOR_SIZE
#define SECTOR_SIZE (1 << SECTOR_SHIFT)
#endif

namespace {
class Uuid
{
public:
    Uuid ()
    {};
    Uuid (const uuid_t & id)
    {
        uuid_copy(m_id, id);
    };
    Uuid (const std::string &idStr)
    {
        uuid_parse(idStr.c_str(), m_id);
    };

    void FromString(const std::string &idStr)
    {
        uuid_parse(idStr.c_str(), m_id);
    };
    const uuid_t& Get() const
    {
        return m_id;
    };
    std::string ToString() const
    {
        char idStr[64];

        uuid_unparse(m_id, idStr);

        return std::string(idStr);
    };
private:
    uuid_t m_id;
};


static int blksnap_fd = 0;
static const char* blksnap_filename = "/dev/" BLK_SNAP_MODULE_NAME;

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

static inline
struct blk_snap_block_range parseRange(const std::string &str)
{
    struct blk_snap_block_range range;
    size_t pos;

    pos = str.find(':');
    if (pos == std::string::npos)
        throw std::invalid_argument("Invalid format of range string.");

    range.sector_offset = std::stoull(str.substr(0, pos));
    range.sector_count = std::stoull(str.substr(pos + 1));

    return range;
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

            std::cout << "allocate range: ofs=" << rg.sector_offset << " cnt=" << rg.sector_count << std::endl;
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

}//namespace

class IArgsProc
{
public:
    IArgsProc()
    {
        m_desc.add_options()
            ("help,h", "[TBD]Print usage for command.");
    };
    virtual ~IArgsProc()
    {};
    virtual void PrintUsage() const
    {
        std::cout << m_usage << std::endl;
        std::cout << m_desc << std::endl;
    };
    virtual void Process(int argc, char** argv)
    {
        po::variables_map vm;
        po::parsed_options parsed = po::command_line_parser(argc, argv).options(m_desc).run();
        po::store(parsed, vm);
        po::notify(vm);

        if (vm.count("help")) {
            PrintUsage();
            return;
        }

        Execute(vm);
    };
    virtual void Execute(po::variables_map &vm) = 0;
protected:
    po::options_description m_desc;
    std::string m_usage;
};

class VersionArgsProc : public IArgsProc
{
public:
    VersionArgsProc()
        :IArgsProc()
    {
        m_usage = std::string("[TBD]Print " BLK_SNAP_MODULE_NAME " module version.");
        m_desc.add_options()
            ("compatibility,c", "[TBD]Print only compatibility flag value in decimal form.")
            ("modification,m", "[TBD]Print only module modification name.")
            ("json,j", "[TBD]Use json format for output.");
    };

    void Execute(po::variables_map &vm) override
    {
        struct blk_snap_version param = {0};

        if (::ioctl(blksnap_fd, IOCTL_BLK_SNAP_VERSION, &param))
            throw std::system_error(errno, std::generic_category(), "Failed to get version.");

        if (vm.count("compatibility")) {
            std::cout << param.compatibility_flags << std::endl;
            return;
        }
        if (vm.count("modification")) {
            std::cout << param.mod_name << std::endl;
            return;
        }

        std::cout << param.major << "."
                  << param.minor << "."
                  << param.revision << "."
                  << param.build << std::endl;
    };
};

class TrackerRemoveArgsProc : public IArgsProc
{
public:
    TrackerRemoveArgsProc()
        :IArgsProc()
    {
        m_usage = std::string("[TBD]Remove block device from change tracking.");
        m_desc.add_options()
            ("device,d", po::value<std::string>(), "[TBD]Device name.");
    };

    void Execute(po::variables_map &vm) override
    {
        struct blk_snap_tracker_remove param;

        if (!vm.count("device"))
            throw std::invalid_argument("Argument 'device' is missed.");

        param.dev_id = deviceByName(vm["device"].as<std::string>());

        if (::ioctl(blksnap_fd, IOCTL_BLK_SNAP_TRACKER_REMOVE, &param))
            throw std::system_error(errno, std::generic_category(),
                "Failed to remove block device from change tracking.");
    };
};

class TrackerCollectArgsProc : public IArgsProc
{
public:
    TrackerCollectArgsProc()
        :IArgsProc()
    {
        m_usage = std::string("[TBD]Collect block devices with change tracking.");
        m_desc.add_options()
            ("json,j", "[TBD]Use json format for output.");
    };

    void Execute(po::variables_map &vm) override
    {
        struct blk_snap_tracker_collect param = {0};
        std::vector<struct blk_snap_cbt_info> cbtInfoVector;

        if (::ioctl(blksnap_fd, IOCTL_BLK_SNAP_TRACKER_COLLECT, &param))
            throw std::system_error(errno, std::generic_category(),
                "[TBD]Failed to collect block devices with change tracking.");

        cbtInfoVector.resize(param.count);
        param.cbt_info_array = cbtInfoVector.data();

        if (::ioctl(blksnap_fd, IOCTL_BLK_SNAP_TRACKER_COLLECT, &param))
            throw std::system_error(errno, std::generic_category(),
                "[TBD]Failed to collect block devices with change tracking.");

        if (vm.count("json"))
            throw std::invalid_argument("Argument 'json' is not supported yet.");

        std::cout << "count=" << param.count << std::endl;
        char generationIdStr[64];
        for (int inx=0; inx<param.count; inx++) {
            struct blk_snap_cbt_info *it = &cbtInfoVector[inx];

            uuid_unparse(it->generationId, generationIdStr);
            std::cout << "," << std::endl;
            std::cout << "device=" << it->dev_id.mj << ":" << it->dev_id.mn << std::endl;
            std::cout << "blk_size=" << it->blk_size << std::endl;
            std::cout << "device_capacity=" << it->device_capacity << std::endl;
            std::cout << "blk_count=" << it->blk_count << std::endl;
            std::cout << "generationId=" << std::string(generationIdStr) << std::endl;
            std::cout << "snap_number=" << static_cast<int>(it->snap_number) << std::endl;
        }
        std::cout << "." << std::endl;
    };
};

class TrackerReadCbtMapArgsProc : public IArgsProc
{
public:
    TrackerReadCbtMapArgsProc()
        :IArgsProc()
    {
        m_usage = std::string("[TBD]Read change tracking map.");
        m_desc.add_options()
            ("device,d", po::value<std::string>(), "[TBD]Device name.")
            ("file,f", po::value<std::string>(), "[TBD]File name for output.")
            ("json,j", "[TBD]Use json format for output.");
    };

    void Execute(po::variables_map &vm) override
    {
        int ret;
        struct blk_snap_tracker_read_cbt_bitmap param;
        std::vector<unsigned char> cbtmap(1024*1024);

        if (!vm.count("device"))
            throw std::invalid_argument("Argument 'device' is missed.");

        param.dev_id = deviceByName(vm["device"].as<std::string>());
        param.offset = 0;
        param.length = cbtmap.size();
        param.buff = cbtmap.data();

        if (vm.count("json"))
            throw std::invalid_argument("Argument 'json' is not supported yet.");

        if (!vm.count("file"))
            throw std::invalid_argument("Argument 'file' is missed.");

        std::ofstream output;
        output.open(vm["file"].as<std::string>(), std::ofstream::out | std::ofstream::binary);

        do {
            ret = ::ioctl(blksnap_fd, IOCTL_BLK_SNAP_TRACKER_READ_CBT_MAP, &param);
            if (ret < 0)
                throw std::system_error(errno, std::generic_category(),
                    "[TBD]Failed to read map of difference from change tracking.");
            if (ret > 0) {
                output.write((char*)param.buff, ret);
                param.offset += ret;
            }
        } while (ret);

        output.close();
    };
};

class TrackerMarkDirtyBlockArgsProc : public IArgsProc
{
public:
    TrackerMarkDirtyBlockArgsProc()
        :IArgsProc()
    {
        m_usage = std::string("[TBD]Mark blocks as changed in change tracking map.");
        m_desc.add_options()
            ("file,f", po::value<std::string>(), "[TBD]File name with dirty blocks.")
            ("device,d", po::value<std::string>(), "[TBD]Device name.")
            ("ranges,r", po::value<std::vector<std::string> >()->multitoken(),
                "[TBD]Sectors range in format 'sector:count'. It's multitoken argument.");
    };

    void Execute(po::variables_map &vm) override
    {
        struct blk_snap_tracker_mark_dirty_blocks param;
        std::vector<struct blk_snap_block_range> ranges;

        if (vm.count("file")) {
            fiemapStorage(vm["file"].as<std::string>(), param.dev_id, ranges);
        } else {
            if (!vm.count("device"))
                throw std::invalid_argument("Argument 'device' is missed.");
            param.dev_id = deviceByName(vm["device"].as<std::string>());

            if (!vm.count("ranges"))
                throw std::invalid_argument("Argument 'ranges' is missed.");
            for (const std::string& range : vm["ranges"].as<std::vector<std::string> >())
                ranges.push_back(parseRange(range));
        }

        param.count = ranges.size();
        param.dirty_blocks_array = ranges.data();

        if (::ioctl(blksnap_fd, IOCTL_BLK_SNAP_TRACKER_MARK_DIRTY_BLOCKS, &param))
            throw std::system_error(errno, std::generic_category(),
                "[TBD]Failed to mark dirty blocks in change tracking map.");
    }
};

class SnapshotCreateArgsProc : public IArgsProc
{
public:
    SnapshotCreateArgsProc()
        :IArgsProc()
    {
        m_usage = std::string("[TBD]Create snapshot object structure.");
        m_desc.add_options()
            ("device,d", po::value<std::vector<std::string> >()->multitoken(),
                "[TBD]Device for snapshot. It's multitoken argument.");
    };

    void Execute(po::variables_map &vm) override
    {
        struct blk_snap_snapshot_create param = {0};
        std::vector<struct blk_snap_dev_t> devices;

        if (!vm.count("device"))
            throw std::invalid_argument("Argument 'device' is missed.");
        for (const std::string& name : vm["device"].as<std::vector<std::string> >())
            devices.push_back(deviceByName(name));

        param.count = devices.size();
        param.dev_id_array = devices.data();

        if (::ioctl(blksnap_fd, IOCTL_BLK_SNAP_SNAPSHOT_CREATE, &param))
            throw std::system_error(errno, std::generic_category(),
                "[TBD]Failed to create snapshot object.");

        char idStr[64];
        uuid_unparse(param.id, idStr);

        std::cout << std::string(idStr) << std::endl;
    };
};

class SnapshotDestroyArgsProc : public IArgsProc
{
public:
    SnapshotDestroyArgsProc()
        :IArgsProc()
    {
        m_usage = std::string("[TBD]Release snapshot and destroy snapshot object.");
        m_desc.add_options()
            ("id,i", po::value<std::string>(), "[TBD]Snapshot uuid.");
    };

    void Execute(po::variables_map &vm) override
    {
        struct blk_snap_snapshot_destroy param;

        if (!vm.count("id"))
            throw std::invalid_argument("Argument 'id' is missed.");

        Uuid id(vm["id"].as<std::string>());
        uuid_copy(param.id, id.Get());

        if (::ioctl(blksnap_fd, IOCTL_BLK_SNAP_SNAPSHOT_DESTROY, &param))
            throw std::system_error(errno, std::generic_category(),
                "[TBD]Failed to destroy snapshot.");
    };
};

class SnapshotAppendStorageArgsProc : public IArgsProc
{
public:
    SnapshotAppendStorageArgsProc()
        :IArgsProc()
    {
        m_usage = std::string("[TBD]Append space in difference storage for snapshot.");
        m_desc.add_options()
            ("id,i", po::value<std::string>(), "[TBD]Snapshot uuid.")
            ("device,d", po::value<std::string>(), "[TBD]Device name.")
            ("range,r", po::value<std::vector<std::string> >()->multitoken(),
                "[TBD]Sectors range in format 'sector:count'. It's multitoken argument.")
            ("file,f", po::value<std::string>(), "[TBD]File for diff storage instead --device.");
    };

    void Execute(po::variables_map &vm) override
    {
        struct blk_snap_snapshot_append_storage param;
        std::vector<struct blk_snap_block_range> ranges;
        struct blk_snap_dev_t dev_id = {0};

        if (!vm.count("id"))
            throw std::invalid_argument("Argument 'id' is missed.");

        Uuid id(vm["id"].as<std::string>());
        uuid_copy(param.id, id.Get());

        if (vm.count("file"))
            fiemapStorage(vm["file"].as<std::string>(), dev_id, ranges);
        else {
            if (!vm.count("device"))
                throw std::invalid_argument("Argument 'device' is missed.");
            dev_id = deviceByName(vm["device"].as<std::string>());

            if (!vm.count("ranges"))
                throw std::invalid_argument("Argument 'ranges' is missed.");
            for (const std::string& range : vm["range"].as<std::vector<std::string> >())
                ranges.push_back(parseRange(range));
        }
        param.dev_id = dev_id;
        param.count = ranges.size();
        param.ranges = ranges.data();
        if (::ioctl(blksnap_fd, IOCTL_BLK_SNAP_SNAPSHOT_APPEND_STORAGE, &param))
            throw std::system_error(errno, std::generic_category(),
                "[TBD]Failed to append storage for snapshot.");
    };
};

class SnapshotTakeArgsProc : public IArgsProc
{
public:
    SnapshotTakeArgsProc()
        :IArgsProc()
    {
        m_usage = std::string("[TBD]Take snapshot.");
        m_desc.add_options()
            ("id,i", po::value<std::string>(), "[TBD]Snapshot uuid.");
    };

    void Execute(po::variables_map &vm) override
    {
        struct blk_snap_snapshot_take param;

        if (!vm.count("id"))
            throw std::invalid_argument("Argument 'id' is missed.");

        Uuid id(vm["id"].as<std::string>());
        uuid_copy(param.id, id.Get());

        if (::ioctl(blksnap_fd, IOCTL_BLK_SNAP_SNAPSHOT_TAKE, &param))
            throw std::system_error(errno, std::generic_category(),
                "[TBD]Failed to append storage for snapshot.");
    };
};

class SnapshotWaitEventArgsProc : public IArgsProc
{
public:
    SnapshotWaitEventArgsProc()
        :IArgsProc()
    {
        m_usage = std::string("[TBD]Wait and read event from snapshot.");
        m_desc.add_options()
            ("id,i", po::value<std::string>(), "[TBD]Snapshot uuid.")
            ("timeout,t", po::value<std::string>(), "[TBD]The allowed waiting time for the event in milliseconds.")
            ("json,j", "[TBD]Use json format for output.");
    };

    void Execute(po::variables_map &vm) override
    {
        struct blk_snap_snapshot_event param;

        if (!vm.count("id"))
            throw std::invalid_argument("Argument 'id' is missed.");

        Uuid id(vm["id"].as<std::string>());
        uuid_copy(param.id, id.Get());

        if (!vm.count("timeout"))
            throw std::invalid_argument("Argument 'timeout' is missed.");
        param.timeout_ms = std::stoi(vm["timeout"].as<std::string>());

        if (::ioctl(blksnap_fd, IOCTL_BLK_SNAP_SNAPSHOT_WAIT_EVENT, &param)) {
            if (errno == ENOENT) {
                if (vm.count("json"))
                    throw std::invalid_argument("Argument 'json' is not supported yet.");

                std::cout << "result=timeout" << std::endl;
            } else if (errno == EINTR) {
                if (vm.count("json"))
                    throw std::invalid_argument("Argument 'json' is not supported yet.");

                std::cout << "result=interrupted" << std::endl;
            } else
                throw std::system_error(errno, std::generic_category(),
                    "[TBD]Failed to get event from snapshot.");
        } else {
            if (vm.count("json"))
                throw std::invalid_argument("Argument 'json' is not supported yet.");

            std::cout << "result=ok" << std::endl;
            std::cout << "time=" << param.time_label << std::endl;

            switch (param.code) {
            case blk_snap_event_low_free_space:
                std::cout << "event=low_free_space" << std::endl;
                std::cout << "requested_nr_sect=" << *(__u64*)(param.data) << std::endl;
                break;
            case blk_snap_event_corrupted:
                std::cout << "event=corrupted" << std::endl;
                break;
            default:
                std::cout << "event=" << param.code << std::endl;
            }
        }
    };
};

class SnapshotCollectArgsProc : public IArgsProc
{
private:
    void CollectSnapshots(std::vector<Uuid> &ids)
    {
        struct blk_snap_snapshot_collect param = {0};

        if (::ioctl(blksnap_fd, IOCTL_BLK_SNAP_SNAPSHOT_COLLECT, &param))
            throw std::system_error(errno, std::generic_category(),
                "[TBD]Failed to get list of active snapshots.");

        if (param.count == 0)
            return;

        std::vector<uuid_t> id_array(param.count);
        param.ids = id_array.data();

        if (::ioctl(blksnap_fd, IOCTL_BLK_SNAP_SNAPSHOT_COLLECT, &param))
            throw std::system_error(errno, std::generic_category(),
                "[TBD]Failed to get list of snapshots.");

        for (int inx=0; inx<param.count; inx++)
            ids.emplace_back(id_array[inx]);
    };

    void CollectImages(const Uuid &id, std::vector<struct blk_snap_image_info> &imageInfoVector)
    {
        struct blk_snap_snapshot_collect_images param = {0};

        uuid_copy(param.id, id.Get());
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
    };

public:
    SnapshotCollectArgsProc()
        :IArgsProc()
    {
        m_usage = std::string("[TBD]Get collection of devices and his snapshot images.");
        m_desc.add_options()
            ("id,i", po::value<std::string>(), "[TBD]Optional parameter snapshot uuid.")
            ("json,j", "[TBD]Use json format for output.");
    };

    void Execute(po::variables_map &vm) override
    {
        std::vector<Uuid> ids;

        if (vm.count("json"))
            throw std::invalid_argument("Argument 'json' is not supported yet.");

        if (vm.count("id"))
            ids.emplace_back(vm["id"].as<std::string>());
        else
            CollectSnapshots(ids);

        for (const Uuid &id : ids) {
            std::cout << "snapshot=" << id.ToString() << std::endl;

            std::vector<struct blk_snap_image_info> imageInfoVector;
            CollectImages(id, imageInfoVector);

            std::cout << "count=" << imageInfoVector.size() << std::endl;
            for (struct blk_snap_image_info &info : imageInfoVector) {

                std::cout << "," << std::endl;
                std::cout << "orig_dev_id=" << info.orig_dev_id.mj << ":" << info.orig_dev_id.mn << std::endl;
                std::cout << "image_dev_id=" << info.image_dev_id.mj << ":" << info.image_dev_id.mn << std::endl;
            }
            std::cout << "." << std::endl;
        }
    };
};

class StretchSnapshotArgsProc : public IArgsProc
{
private:
    Uuid m_id;
    std::string m_path;
    std::vector<std::string> m_allocated_sectFiles;
    int m_counter;
    unsigned long long m_allocated_sect;
    unsigned long long m_limit_sect;
private:
    void ProcessLowFreeSpace(unsigned int time_label, struct blk_snap_event_low_free_space *data)
    {
        std::string filename;
        int fd;

        std::cout << time_label << " - Low free space in diff storage. Requested "
            << data->requested_nr_sect << " sectors." << std::endl;

        if (m_allocated_sect > m_limit_sect) {
            std::cerr << "The diff storage limit has been achieved." << std::endl;
            return;
        }

        fs::path filepath(m_path);
        filepath += "diff_storage#";
        filepath += std::to_string(m_counter++);
        filename = filepath.string();

        fd = ::open(filename.c_str(), O_CREAT | O_RDWR | O_EXCL | O_LARGEFILE);
        if (fd < 0)
                throw std::system_error(errno, std::generic_category(),
                                        "[TBD]Failed to create file for diff storage.");
        m_allocated_sectFiles.push_back(filename);

        if (::fallocate64(fd, 0, 0, data->requested_nr_sect * SECTOR_SIZE)) {
            int err = errno;

            ::close(fd);
            throw std::system_error(err, std::generic_category(),
                                    "[TBD]Failed to allocate file for diff storage.");
        }
        ::close(fd);
        m_allocated_sect += data->requested_nr_sect;

        std::vector<struct blk_snap_block_range> ranges;
        struct blk_snap_dev_t dev_id = {0};
        fiemapStorage(filename, dev_id, ranges);

        struct blk_snap_snapshot_append_storage param;
        uuid_copy(param.id, m_id.Get());
        param.dev_id = dev_id;
        param.count = ranges.size();
        param.ranges = ranges.data();

        if (::ioctl(blksnap_fd, IOCTL_BLK_SNAP_SNAPSHOT_APPEND_STORAGE, &param))
            throw std::system_error(errno, std::generic_category(),
                "[TBD]Failed to append storage for snapshot.");
    };

    void ProcessEventCorrupted(unsigned int time_label, struct blk_snap_event_corrupted *data)
    {
        std::cout << time_label << " - The snapshot was corrupted for device [" <<
            data->orig_dev_id.mj << ":" << data->orig_dev_id.mn << "] with error \"" <<
            std::strerror(data->err_code) << "\"." << std::endl;
    };

public:
    StretchSnapshotArgsProc()
        :IArgsProc()
    {
        m_usage = std::string("[TBD]Start stretch snapshot service.");
        m_desc.add_options()
            ("id,i", po::value<std::string>(), "[TBD]Snapshot uuid.")
            ("path,p", po::value<std::string>(), "[TBD]Path for diff storage files.")
            ("limit,l", po::value<unsigned int>(), "[TBD]Available diff storage size in MiB.");
    };

    void Execute(po::variables_map &vm) override
    {
        bool terminate = false;
        struct blk_snap_snapshot_event param;

        if (!vm.count("id"))
            throw std::invalid_argument("Argument 'id' is missed.");

        m_id.FromString(vm["id"].as<std::string>());

        if (!vm.count("path"))
            throw std::invalid_argument("Argument 'path' is missed.");
        m_path = vm["path"].as<std::string>();

        if (vm.count("limit"))
            m_limit_sect = (1024ULL * 1024 / SECTOR_SIZE) * vm["limit"].as<unsigned int>();
        else
            m_limit_sect = -1ULL;

        std::cout << "Stretch snapshot service started." << std::endl;

        try {
            uuid_copy(param.id, m_id.Get());
            param.timeout_ms = 1000;
            m_counter = 0;
            while (!terminate) {
                if (::ioctl(blksnap_fd, IOCTL_BLK_SNAP_SNAPSHOT_WAIT_EVENT, &param)) {
                    int err = errno;

                    if ((err == ENOENT) || (err == EINTR))
                        continue;

                    throw std::system_error(err, std::generic_category(),
                            "[TBD]Failed to get event from snapshot.");
                }

                switch (param.code) {
                case blk_snap_event_low_free_space:
                    ProcessLowFreeSpace(param.time_label, (struct blk_snap_event_low_free_space *)param.data);
                    break;
                case blk_snap_event_corrupted:
                    ProcessEventCorrupted(param.time_label, (struct blk_snap_event_corrupted *)param.data);
                    terminate = true;
                    break;
                default:
                    std::cout << param.time_label << " - unsupported event #" << param.code << "." << std::endl;
                }
            }

            for (const std::string &filename : m_allocated_sectFiles)
                if (::remove(filename.c_str()))
                    std::cout << "Failed to cleanup diff storage file \"" << filename << "\". " << std::strerror(errno) << std::endl;
        }
        catch(std::exception& ex)
        {
            std::cerr << ex.what() << std::endl;
            throw std::runtime_error("Stretch snapshot service failed.");
        }
        std::cout << "Stretch snapshot service finished." << std::endl;
    };
};

static
std::map<std::string, std::shared_ptr<IArgsProc> > argsProcMap {
    {"version", std::make_shared<VersionArgsProc>()},
    {"tracker_remove", std::make_shared<TrackerRemoveArgsProc>()},
    {"tracker_collect", std::make_shared<TrackerCollectArgsProc>()},
    {"tracker_readcbtmap", std::make_shared<TrackerReadCbtMapArgsProc>()},
    {"tracker_markdirtyblock", std::make_shared<TrackerMarkDirtyBlockArgsProc>()},
    {"snapshot_create", std::make_shared<SnapshotCreateArgsProc>()},
    {"snapshot_destroy", std::make_shared<SnapshotDestroyArgsProc>()},
    {"snapshot_appendstorage", std::make_shared<SnapshotAppendStorageArgsProc>()},
    {"snapshot_take", std::make_shared<SnapshotTakeArgsProc>()},
    {"snapshot_waitevent", std::make_shared<SnapshotWaitEventArgsProc>()},
    {"snapshot_collect", std::make_shared<SnapshotCollectArgsProc>()},
    {"stretch_snapshot", std::make_shared<StretchSnapshotArgsProc>()},
};

static
void printUsage()
{
    std::cout << "[TBD]Usage:" << std::endl;
    std::cout << "--help, -h or help:" << std::endl;
    std::cout << "\tPrint this usage." << std::endl;
    std::cout << "<command> [arguments]:" << std::endl;
    std::cout << "\tExecute the management command." << std::endl;
    std::cout << std::endl;
    std::cout << "Available commands with arguments:" << std::endl;
    for (const auto &it : argsProcMap) {
        std::cout << it.first << ":" << std::endl;
        it.second->PrintUsage();
    }
}

static
void process(int argc, char** argv)
{
    if (argc < 2)
        throw std::runtime_error("[TBD]Command not found.");

    std::string commandName(argv[1]);
    const auto &itArgsProc = argsProcMap.find(commandName);
    if (itArgsProc != argsProcMap.end()) {
        itArgsProc->second->Process(--argc, ++argv);
        return;
    }

    if ((commandName == "help") || (commandName == "--help") || (commandName == "-h")) {
        printUsage();
        return;
    }

    throw std::runtime_error("Command is not set.");
}

int main(int argc, char* argv[])
{
    int ret = 0;

    try
    {
        blksnap_fd = ::open(blksnap_filename, O_RDWR);
        if (blksnap_fd < 0)
            throw std::system_error(errno, std::generic_category(), blksnap_filename);

        process(argc, argv);
    }
    catch(std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
        ret = 1;
    }
    if (blksnap_fd > 0)
        ::close(blksnap_fd);

    return ret;
}
