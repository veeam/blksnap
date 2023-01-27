// SPDX-License-Identifier: GPL-2.0+
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <linux/fiemap.h>
#include <linux/fs.h>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <vector>
#include <blksnap/blksnap.h>
#include <time.h>

namespace po = boost::program_options;
namespace fs = boost::filesystem;

#ifndef SECTOR_SHIFT
#    define SECTOR_SHIFT 9
#endif
#ifndef SECTOR_SIZE
#    define SECTOR_SIZE (1 << SECTOR_SHIFT)
#endif

namespace
{
    class Uuid
    {
    public:
        Uuid(){};
        Uuid(const uuid_t& id)
        {
            uuid_copy(m_id, id);
        };
        Uuid(const __u8 buf[16])
        {
            memcpy(m_id, buf, sizeof(uuid_t));
        };
        Uuid(const std::string& idStr)
        {
            uuid_parse(idStr.c_str(), m_id);
        };

        void FromString(const std::string& idStr)
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

    class CBlksnapFileWrap
    {
    public:
        CBlksnapFileWrap()
            : m_blksnapFd(0)
        {
            const char* blksnap_filename = "/dev/" BLKSNAP_CTL;

            m_blksnapFd = ::open(blksnap_filename, O_RDWR);
            if (m_blksnapFd < 0)
                throw std::system_error(errno, std::generic_category(), blksnap_filename);
        };
        ~CBlksnapFileWrap()
        {
            if (m_blksnapFd > 0)
                ::close(m_blksnapFd);
        };
        int get() const
        {
            return m_blksnapFd;
        };

    private:
        int m_blksnapFd;

    };

    class CDeviceCtl
    {
    public:
        CDeviceCtl(const std::string& devicePath)
            : m_fd(0)
        {
            m_fd = ::open(devicePath.c_str(), O_DIRECT, 0600);
            if (m_fd < 0)
                throw std::system_error(errno, std::generic_category(), "Failed to open block device ["+devicePath+"].");
        };

        virtual ~CDeviceCtl()
        {
            if (m_fd > 0)
                ::close(m_fd);
        };

        unsigned int Ioctl(const unsigned int cmd, void *param)
        {
            int ret = ::ioctl(m_fd, cmd, param);
            if (ret < 0)
                throw std::system_error(errno, std::generic_category());
            return static_cast<unsigned int>(ret);
        }

    private:
        int m_fd;

    };

    class CBlkFilterCtl
    {
    public:
        CBlkFilterCtl(const std::string& devicePath)
            : deviceCtl(devicePath)
        {};

        bool Attach()
        {
            struct blkfilter_ctl ctl = {
                .name = {'b','l','k','s','n','a','p', '\0'},
                .cmd = BLKFILTER_CMD_ATTACH,
                .optlen = 0,
                .opt = nullptr,
            };

            try
            {
                deviceCtl.Ioctl(BLKFILTER, &ctl);
            }
            catch (std::system_error &ex)
            {
                if (ex.code() == std::error_code(EALREADY, std::generic_category()))
                    return true;

                throw std::runtime_error(std::string("Failed to attach filter to the block device: ") + ex.what());
            }

            return false;
        };

        void Detach()
        {
            struct blkfilter_ctl ctl = {
                .name = {'b','l','k','s','n','a','p', '\0'},
                .cmd = BLKFILTER_CMD_DETACH,
                .optlen = 0,
                .opt = nullptr,
            };

            try
            {
                deviceCtl.Ioctl(BLKFILTER, &ctl);
            }
            catch (std::exception &ex)
            {
                throw std::runtime_error(
                    std::string("Failed to detach filter from the block device: ") + ex.what());
            }
        };

        unsigned int Control(const unsigned int cmd, void *buf, unsigned int len)
        {
            struct blkfilter_ctl ctl = {
                .name = {'b','l','k','s','n','a','p', '\0'},
                .cmd = BLKFILTER_CMD_CTL + cmd,
                .optlen = len,
                .opt = buf,
            };

            deviceCtl.Ioctl(BLKFILTER, &ctl);

            return ctl.optlen;
        };
    private:
        CDeviceCtl deviceCtl;
    };


    static inline struct blksnap_sectors parseRange(const std::string& str)
    {
        struct blksnap_sectors range;
        size_t pos;

        pos = str.find(':');
        if (pos == std::string::npos)
            throw std::invalid_argument("Invalid format of range string.");

        range.offset = std::stoull(str.substr(0, pos));
        range.count = std::stoull(str.substr(pos + 1));

        return range;
    }

    static void fiemapStorage(const std::string& filename, std::string& devicePath,
                              std::vector<struct blksnap_sectors>& ranges)
    {
        int ret = 0;
        const char* errMessage;
        int fd = -1;
        struct fiemap* map = NULL;
        int extentMax = 500;
        long long fileSize;
        struct stat64 st;

        if (::stat64(filename.c_str(), &st))
            throw std::system_error(errno, std::generic_category(), "Failed to get file size.");

        fileSize = st.st_size;
        devicePath = std::string("/dev/block/") +
            std::to_string(major(st.st_dev)) + ":" +
            std::to_string(minor(st.st_dev));
        std::cout << "device path: '" << devicePath << "'" << std::endl;

        fd = ::open(filename.c_str(), O_RDONLY | O_EXCL | O_LARGEFILE);
        if (fd < 0)
        {
            ret = errno;
            errMessage = "Failed to open file.";
            goto out;
        }

        map = (struct fiemap*)::malloc(sizeof(struct fiemap) + sizeof(struct fiemap_extent) * extentMax);
        if (!map)
        {
            ret = ENOMEM;
            errMessage = "Failed to allocate memory for fiemap structure.";
            goto out;
        }

        for (long long fileOffset = 0; fileOffset < fileSize;)
        {
            map->fm_start = fileOffset;
            map->fm_length = fileSize - fileOffset;
            map->fm_extent_count = extentMax;
            map->fm_flags = 0;

            if (::ioctl(fd, FS_IOC_FIEMAP, map))
            {
                ret = errno;
                errMessage = "Failed to call FS_IOC_FIEMAP.";
                goto out;
            }

            for (int i = 0; i < map->fm_mapped_extents; ++i)
            {
                struct blksnap_sectors rg;
                struct fiemap_extent* extent = map->fm_extents + i;

                if (extent->fe_physical & (SECTOR_SIZE - 1))
                {
                    ret = EINVAL;
                    errMessage = "File location is not ordered by sector size.";
                    goto out;
                }

                rg.offset = extent->fe_physical >> SECTOR_SHIFT;
                rg.count = extent->fe_length >> SECTOR_SHIFT;
                ranges.push_back(rg);

                fileOffset = extent->fe_logical + extent->fe_length;

                std::cout << "allocate range: ofs=" << rg.offset << " cnt=" << rg.count << std::endl;
            }
        }

    out:
        if (map)
            ::free(map);
        if (fd >= 0)
            ::close(fd);
        if (ret)
            throw std::system_error(ret, std::generic_category(), errMessage);
    }
} // namespace

class IArgsProc
{
public:
    IArgsProc()
    {
        m_desc.add_options()
            ("help,h", "Print usage for command.");
    };
    virtual ~IArgsProc(){};
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

        if (vm.count("help"))
        {
            PrintUsage();
            return;
        }

        Execute(vm);
    };
    virtual void Execute(po::variables_map& vm) = 0;

protected:
    po::options_description m_desc;
    std::string m_usage;
};

#ifdef BLKSNAP_MODIFICATION
#pragma message "BLKSNAP_MODIFICATION defined"
#endif

class VersionArgsProc : public IArgsProc
{
public:
    VersionArgsProc()
        : IArgsProc()
    {
        m_usage = std::string("Print module version.");
        m_desc.add_options()
#ifdef BLKSNAP_MODIFICATION
          ("modification,m", "Print module modification name.")
          ("compatibility,c", "Print compatibility flag value in decimal form.")
#endif
          ("json,j", "Use json format for output.");
    };

    void Execute(po::variables_map& vm) override
    {
        CBlksnapFileWrap blksnapFd;
#ifdef BLKSNAP_MODIFICATION
        bool isModification = (vm.count("modification") != 0);
        bool isCompatibility = (vm.count("compatibility") != 0);

        if (isModification || isCompatibility)
        {
            struct blksnap_mod param = {0};

            if (::ioctl(blksnapFd.get(), IOCTL_BLKSNAP_MOD, &param))
                throw std::system_error(errno, std::generic_category(), "Failed to get modification or compatibility information.");

            if (isModification)
                std::cout << param.name << std::endl;

            if (isCompatibility) {
                if (param.compatibility_flags & (1ull << blksnap_compat_flag_debug_sector_state))
                    std::cout << "debug_sector_state" << std::endl;

                if (param.compatibility_flags & (1ull << blksnap_compat_flag_setlog))
                    std::cout << "setlog" << std::endl;
            }
            return;
        }
#endif
        {
            struct blksnap_version param = {0};

            if (::ioctl(blksnapFd.get(), IOCTL_BLKSNAP_VERSION, &param))
                throw std::system_error(errno, std::generic_category(), "Failed to get version.");

            std::cout << param.major << "." << param.minor << "." << param.revision << "." << param.build << std::endl;
        }
    };
};

class AttachArgsProc : public IArgsProc
{
public:
    AttachArgsProc()
        : IArgsProc()
    {
        m_usage = std::string("Attach blksnap tracker to block device.");
        m_desc.add_options()
            ("device,d", po::value<std::string>(), "Device name.");
    };

    void Execute(po::variables_map& vm) override
    {
        if (!vm.count("device"))
            throw std::invalid_argument("Argument 'device' is missed.");

        CBlkFilterCtl(vm["device"].as<std::string>()).Attach();
    };
};

class DetachArgsProc : public IArgsProc
{
public:
    DetachArgsProc()
        : IArgsProc()
    {
        m_usage = std::string("Detach blksnap tracker from block device.");
        m_desc.add_options()
            ("device,d", po::value<std::string>(), "Device name.");
    };

    void Execute(po::variables_map& vm) override
    {
        if (!vm.count("device"))
            throw std::invalid_argument("Argument 'device' is missed.");

        CBlkFilterCtl(vm["device"].as<std::string>()).Detach();
    };
};

class CbtInfoArgsProc : public IArgsProc
{
public:
    CbtInfoArgsProc()
        : IArgsProc()
    {
        m_usage = std::string("Get change tracker information.");
        m_desc.add_options()
            ("device,d", po::value<std::string>(), "Device name.")
            ("json,j", "Use json format for output.");
    };
    void Execute(po::variables_map& vm) override
    {
        if (!vm.count("device"))
            throw std::invalid_argument("Argument 'device' is missed.");

        CBlkFilterCtl ctl(vm["device"].as<std::string>());

        if (vm.count("json"))
            throw std::invalid_argument("Argument 'json' is not supported yet.");

        struct blksnap_cbtinfo info;
        ctl.Control(blkfilter_ctl_blksnap_cbtinfo, &info, sizeof(info));

        std::cout << "block_size=" << info.block_size << std::endl;
        std::cout << "device_capacity=" << info.device_capacity << std::endl;
        std::cout << "block_count=" << info.block_count << std::endl;
        char generationIdStr[64];
        uuid_unparse(info.generation_id.b, generationIdStr);
        std::cout << "generation_id=" << std::string(generationIdStr) << std::endl;
        std::cout << "changes_number=" << static_cast<int>(info.changes_number) << std::endl;
    };
};

class ReadCbtMapArgsProc : public IArgsProc
{
public:
    ReadCbtMapArgsProc()
        : IArgsProc()
    {
        m_usage = std::string("Read change tracking map.");
        m_desc.add_options()
            ("device,d", po::value<std::string>(), "Device name.")
            ("file,f", po::value<std::string>(), "File name for output.")
            ("json,j", "Use json format for output.");
    };

    void Execute(po::variables_map& vm) override
    {
        unsigned int elapsed;

        if (!vm.count("device"))
            throw std::invalid_argument("Argument 'device' is missed.");

        CBlkFilterCtl ctl(vm["device"].as<std::string>());

        struct blksnap_cbtinfo info;
        ctl.Control(blkfilter_ctl_blksnap_cbtinfo, &info, sizeof(info));
        elapsed = info.block_count;

        if (vm.count("json"))
            throw std::invalid_argument("Argument 'json' is not supported yet.");

        if (!vm.count("file"))
            throw std::invalid_argument("Argument 'file' is missed.");

        std::ofstream output;
        output.open(vm["file"].as<std::string>(), std::ofstream::out | std::ofstream::binary);

        std::vector<unsigned char> buf(std::min(32*1024u, elapsed));
        struct blksnap_cbtmap arg = {
            .offset = 0,
            .buffer = buf.data()
        };
        while (elapsed) {
            arg.length = std::min(static_cast<unsigned int>(buf.size()), elapsed);

            ctl.Control(blkfilter_ctl_blksnap_cbtmap, &arg, sizeof(struct blksnap_cbtmap));

            elapsed -= arg.length;
            arg.offset += arg.length;

            output.write(reinterpret_cast<char *>(arg.buffer), arg.length);
        };

        output.close();
    };
};

class MarkDirtyBlockArgsProc : public IArgsProc
{
public:
    MarkDirtyBlockArgsProc()
        : IArgsProc()
    {
        m_usage = std::string("Mark blocks as changed in change tracking map.");
        m_desc.add_options()
            ("file,f", po::value<std::string>(), "File name with dirty blocks.")
            ("device,d", po::value<std::string>(), "Device name.")
            ("ranges,r", po::value<std::vector<std::string>>()->multitoken(), "Sectors range in format 'sector:count'. It's multitoken argument.");
    };

    void Execute(po::variables_map& vm) override
    {
        std::string devicePath;
        std::vector<struct blksnap_sectors> ranges;

        if (vm.count("file"))
        {
            fiemapStorage(vm["file"].as<std::string>(), devicePath, ranges);
        }
        else
        {
            if (!vm.count("device"))
                throw std::invalid_argument("Argument 'device' is missed.");

            devicePath = vm["device"].as<std::string>();

            if (!vm.count("ranges"))
                throw std::invalid_argument("Argument 'ranges' is missed.");

            for (const std::string& range : vm["ranges"].as<std::vector<std::string>>())
                ranges.push_back(parseRange(range));
        }

        CBlkFilterCtl ctl(devicePath);
        struct blksnap_cbtdirty arg = {
            .count = static_cast<unsigned int>(ranges.size()),
            .dirty_sectors = ranges.data()
        };
        ctl.Control(blkfilter_ctl_blksnap_cbtdirty, &arg, sizeof(arg));
    }
};

class SnapshotInfoArgsProc : public IArgsProc
{
public:
    SnapshotInfoArgsProc()
        : IArgsProc()
    {
        m_usage = std::string("Get information about block device snapshot image.");
        m_desc.add_options()
            ("device,d", po::value<std::string>(), "Device name.")
            ("field,f", po::value<std::string>(), "Out only selected field.")
            ("json,j", "Use json format for output.");
    };

    void Execute(po::variables_map& vm) override
    {
        if (!vm.count("device"))
            throw std::invalid_argument("Argument 'device' is missed.");

        CBlkFilterCtl ctl(vm["device"].as<std::string>());

        if (vm.count("json"))
            throw std::invalid_argument("Argument 'json' is not supported yet.");

        struct blksnap_snapshotinfo param;
        ctl.Control(blkfilter_ctl_blksnap_snapshotinfo, &param, sizeof(param));

        std::vector<char> image(IMAGE_DISK_NAME_LEN + 1);
        strncpy(image.data(), reinterpret_cast<char *>(param.image), IMAGE_DISK_NAME_LEN);

        if (vm.count("field")) {
            std::string field=vm["field"].as<std::string>();

            if (field == "image")
                std::cout << std::string("/dev/") + std::string(image.data()) << std::endl;
            else if (field == "error_code")
                std::cout << param.error_code << std::endl;
            else
                throw std::invalid_argument("Value '"+field+"' for argument '--field' is not supported.");
        }
        else
        {
            std::cout << "error_code=" << param.error_code << std::endl;
            std::cout << "image=" << std::string("/dev/") + std::string(image.data()) << std::endl;
        }
    };
};

static inline void SnapshotAdd(const uuid_t& id, const std::string& devicePath)
{
    struct blksnap_snapshotadd param;
    bool retry = false;

    uuid_copy(param.id.b, id);
    CBlkFilterCtl ctl(devicePath);

    do {
        try
        {
            ctl.Control(blkfilter_ctl_blksnap_snapshotadd, &param, sizeof(param));
            retry = false;
        }
        catch(std::system_error &ex)
        {
            if ((ex.code() == std::error_code(ENOENT, std::generic_category())) && !retry)
                retry = true;
            else
                throw ex;
        }

        if (retry)
            ctl.Attach();
    } while (retry);
}

class SnapshotAddArgsProc : public IArgsProc
{
public:
    SnapshotAddArgsProc()
        : IArgsProc()
    {
        m_usage = std::string("Create snapshot object structure.");
        m_desc.add_options()
            ("device,d", po::value<std::string>(), "Device name.")
            ("id,i", po::value<std::string>(), "Snapshot uuid.");
    };

    void Execute(po::variables_map& vm) override
    {
        if (!vm.count("device"))
            throw std::invalid_argument("Argument 'device' is missed.");

        if (!vm.count("id"))
            throw std::invalid_argument("Argument 'id' is missed.");

        SnapshotAdd(Uuid(vm["id"].as<std::string>()).Get(), vm["device"].as<std::string>());
    };
};

class SnapshotCreateArgsProc : public IArgsProc
{
public:
    SnapshotCreateArgsProc()
        : IArgsProc()
    {
        m_usage = std::string("Create snapshot object structure.");
        m_desc.add_options()
            ("device,d", po::value<std::vector<std::string>>()->multitoken(), "Device name for snapshot. It's multitoken argument.");
    };

    void Execute(po::variables_map& vm) override
    {
        CBlksnapFileWrap blksnapFd;
        struct blksnap_uuid param = {0};
        std::vector<std::string> devices;

        if (vm.count("device"))
            devices = vm["device"].as<std::vector<std::string>>();

        if (::ioctl(blksnapFd.get(), IOCTL_BLKSNAP_SNAPSHOT_CREATE, &param))
            throw std::system_error(errno, std::generic_category(), "Failed to create snapshot object.");

        Uuid id(param.b);
        std::cout << id.ToString() << std::endl;

        if (vm.count("device"))
            for (const std::string& devicePath : devices)
                SnapshotAdd(id.Get(), devicePath);
    };
};

class SnapshotDestroyArgsProc : public IArgsProc
{
public:
    SnapshotDestroyArgsProc()
        : IArgsProc()
    {
        m_usage = std::string("Release snapshot and destroy snapshot object.");
        m_desc.add_options()
            ("id,i", po::value<std::string>(), "Snapshot uuid.");
    };

    void Execute(po::variables_map& vm) override
    {
        CBlksnapFileWrap blksnapFd;
        struct blksnap_uuid param;

        if (!vm.count("id"))
            throw std::invalid_argument("Argument 'id' is missed.");

        uuid_copy(param.b, Uuid(vm["id"].as<std::string>()).Get());

        if (::ioctl(blksnapFd.get(), IOCTL_BLKSNAP_SNAPSHOT_DESTROY, &param))
            throw std::system_error(errno, std::generic_category(), "Failed to destroy snapshot.");
    };
};

static inline void AppendStorage(const int blksnapFd, const Uuid& id, const std::string& devicePath, std::vector<struct blksnap_sectors>& ranges)
{
    struct blksnap_snapshot_append_storage param;

    uuid_copy(param.id.b, id.Get());

    unsigned int size = devicePath.size();
    std::unique_ptr<char []> bdev_path(new char[size+1]);
    strncpy(bdev_path.get(), devicePath.c_str(), size);
    bdev_path[size] = '\0';

    param.bdev_path = reinterpret_cast<__s8 *>(bdev_path.get());
    param.bdev_path_size = size + 1;
    param.count = ranges.size();
    param.ranges = ranges.data();
    if (::ioctl(blksnapFd, IOCTL_BLKSNAP_SNAPSHOT_APPEND_STORAGE, &param))
        throw std::system_error(errno, std::generic_category(), "Failed to append storage for snapshot");
}

class SnapshotAppendStorageArgsProc : public IArgsProc
{
public:
    SnapshotAppendStorageArgsProc()
        : IArgsProc()
    {
        m_usage = std::string("Append space in difference storage for snapshot.");
        m_desc.add_options()
            ("id,i", po::value<std::string>(), "Snapshot uuid.")
            ("device,d", po::value<std::string>(), "Device name.")
            ("range,r", po::value<std::vector<std::string>>()->multitoken(), "Sectors range in format 'sector:count'. It's multitoken argument.")
            ("file,f", po::value<std::string>(), "File for diff storage instead --device.");
    };

    void Execute(po::variables_map& vm) override
    {
        CBlksnapFileWrap blksnapFd;

        std::vector<struct blksnap_sectors> ranges;
        std::string devicePath;

        if (!vm.count("id"))
            throw std::invalid_argument("Argument 'id' is missed.");

        Uuid id(vm["id"].as<std::string>());


        if (vm.count("file"))
            fiemapStorage(vm["file"].as<std::string>(), devicePath, ranges);
        else
        {
            if (!vm.count("device"))
                throw std::invalid_argument("Argument 'device' is missed.");
            devicePath = vm["device"].as<std::string>();

            if (!vm.count("ranges"))
                throw std::invalid_argument("Argument 'ranges' is missed.");

            for (const std::string& range : vm["range"].as<std::vector<std::string>>())
                ranges.push_back(parseRange(range));
        }

        AppendStorage(blksnapFd.get(), id, devicePath, ranges);
    };
};

class SnapshotTakeArgsProc : public IArgsProc
{
public:
    SnapshotTakeArgsProc()
        : IArgsProc()
    {
        m_usage = std::string("Take snapshot.");
        m_desc.add_options()
            ("id,i", po::value<std::string>(), "Snapshot uuid.");
    };

    void Execute(po::variables_map& vm) override
    {
        CBlksnapFileWrap blksnapFd;
        struct blksnap_uuid param;

        if (!vm.count("id"))
            throw std::invalid_argument("Argument 'id' is missed.");

        uuid_copy(param.b, Uuid(vm["id"].as<std::string>()).Get());

        if (::ioctl(blksnapFd.get(), IOCTL_BLKSNAP_SNAPSHOT_TAKE, &param))
            throw std::system_error(errno, std::generic_category(), "Failed to take snapshot.");
    };
};

class SnapshotWaitEventArgsProc : public IArgsProc
{
public:
    SnapshotWaitEventArgsProc()
        : IArgsProc()
    {
        m_usage = std::string("Wait and read event from snapshot.");
        m_desc.add_options()
            ("id,i", po::value<std::string>(), "Snapshot uuid.")
            ("timeout,t", po::value<std::string>(), "The allowed waiting time for the event in milliseconds.")
            ("json,j", "Use json format for output.");
    };

    void Execute(po::variables_map& vm) override
    {
        CBlksnapFileWrap blksnapFd;
        struct blksnap_snapshot_event param;

        if (!vm.count("id"))
            throw std::invalid_argument("Argument 'id' is missed.");

        uuid_copy(param.id.b, Uuid(vm["id"].as<std::string>()).Get());

        if (!vm.count("timeout"))
            throw std::invalid_argument("Argument 'timeout' is missed.");
        param.timeout_ms = std::stoi(vm["timeout"].as<std::string>());

        if (::ioctl(blksnapFd.get(), IOCTL_BLKSNAP_SNAPSHOT_WAIT_EVENT, &param))
        {
            if (errno == ENOENT)
            {
                if (vm.count("json"))
                    throw std::invalid_argument("Argument 'json' is not supported yet.");

                std::cout << "result=timeout" << std::endl;
            }
            else if (errno == EINTR)
            {
                if (vm.count("json"))
                    throw std::invalid_argument("Argument 'json' is not supported yet.");

                std::cout << "result=interrupted" << std::endl;
            }
            else
                throw std::system_error(errno, std::generic_category(), "Failed to get event from snapshot.");
        }
        else
        {
            if (vm.count("json"))
                throw std::invalid_argument("Argument 'json' is not supported yet.");

            std::cout << "result=ok" << std::endl;
            std::cout << "time=" << param.time_label << std::endl;

            switch (param.code)
            {
            case blksnap_event_code_low_free_space:
                std::cout << "event=low_free_space" << std::endl;
                std::cout << "requested_nr_sect=" << *(__u64*)(param.data) << std::endl;
                break;
            case blksnap_event_code_corrupted:
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
    void CollectSnapshots(std::vector<Uuid>& ids)
    {
        CBlksnapFileWrap blksnapFd;
        struct blksnap_snapshot_collect param = {0};

        if (::ioctl(blksnapFd.get(), IOCTL_BLKSNAP_SNAPSHOT_COLLECT, &param))
            throw std::system_error(errno, std::generic_category(), "Failed to get list of active snapshots.");

        if (param.count == 0)
            return;

        std::vector<struct blksnap_uuid> id_array(param.count);
        param.ids = id_array.data();

        if (::ioctl(blksnapFd.get(), IOCTL_BLKSNAP_SNAPSHOT_COLLECT, &param))
            throw std::system_error(errno, std::generic_category(), "Failed to get list of snapshots.");

        for (size_t inx = 0; inx < param.count; inx++)
            ids.emplace_back(id_array[inx].b);
    };

public:
    SnapshotCollectArgsProc()
        : IArgsProc()
    {
        m_usage = std::string("Get collection of devices and his snapshot images.");
        m_desc.add_options()
            ("json,j", "Use json format for output.");
    };

    void Execute(po::variables_map& vm) override
    {
        if (vm.count("json"))
            throw std::invalid_argument("Argument 'json' is not supported yet.");

        std::vector<Uuid> ids;
        CollectSnapshots(ids);
        for (const Uuid& id : ids)
            std::cout << id.ToString() << " " << std::endl;
        std::cout << std::endl;
            //std::cout << "id=" << id.ToString() << std::endl;
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
    void ProcessLowFreeSpace(const CBlksnapFileWrap& blksnapFd, unsigned int time_label, struct blksnap_event_low_free_space* data)
    {
        std::string filename;
        int fd;

        std::cout << time_label << " - Low free space in diff storage. Requested " << data->requested_nr_sect
                  << " sectors." << std::endl;

        if (m_allocated_sect > m_limit_sect)
        {
            std::cerr << "The diff storage limit has been achieved." << std::endl;
            return;
        }

        fs::path filepath(m_path);
        filepath += "diff_storage#";
        filepath += std::to_string(m_counter++);
        if (fs::exists(filepath))
            fs::remove(filepath);
        filename = filepath.string();

        fd = ::open(filename.c_str(), O_CREAT | O_RDWR | O_EXCL | O_LARGEFILE, 0600);
        if (fd < 0)
            throw std::system_error(errno, std::generic_category(), "Failed to create file for diff storage.");
        m_allocated_sectFiles.push_back(filename);

        if (::fallocate64(fd, 0, 0, data->requested_nr_sect * SECTOR_SIZE))
        {
            int err = errno;

            ::close(fd);
            throw std::system_error(err, std::generic_category(), "Failed to allocate file for diff storage.");
        }
        ::close(fd);
        m_allocated_sect += data->requested_nr_sect;

        std::vector<struct blksnap_sectors> ranges;
        std::string devicePath;

        fiemapStorage(filename, devicePath, ranges);

        AppendStorage(blksnapFd.get(), m_id, devicePath, ranges);
    };

    void ProcessEventCorrupted(unsigned int time_label, struct blksnap_event_corrupted* data)
    {
        std::cout << time_label << " - The snapshot was corrupted for device [" << data->dev_id.major << ":"
                  << data->dev_id.minor << "] with error \"" << std::strerror(data->err_code) << "\"." << std::endl;
    };

public:
    StretchSnapshotArgsProc()
        : IArgsProc()
    {
        m_usage = std::string("Start stretch snapshot service.");
        m_desc.add_options()
            ("id,i", po::value<std::string>(), "Snapshot uuid.")
            ("path,p", po::value<std::string>(), "Path for diff storage files.")
            ("limit,l", po::value<unsigned int>(), "Available diff storage size in MiB.");
    };

    void Execute(po::variables_map& vm) override
    {
        CBlksnapFileWrap blksnapFd;
        bool terminate = false;
        struct blksnap_snapshot_event param;

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

        try
        {
            uuid_copy(param.id.b, m_id.Get());
            param.timeout_ms = 1000;
            m_counter = 0;
            while (!terminate)
            {
                if (::ioctl(blksnapFd.get(), IOCTL_BLKSNAP_SNAPSHOT_WAIT_EVENT, &param))
                {
                    int err = errno;

                    if ((err == ENOENT) || (err == EINTR))
                        continue;

                    throw std::system_error(err, std::generic_category(), "Failed to get event from snapshot.");
                }

                switch (param.code)
                {
                case blksnap_event_code_low_free_space:
                    ProcessLowFreeSpace(blksnapFd, param.time_label, (struct blksnap_event_low_free_space*)param.data);
                    break;
                case blksnap_event_code_corrupted:
                    ProcessEventCorrupted(param.time_label, (struct blksnap_event_corrupted*)param.data);
                    terminate = true;
                    break;
                default:
                    std::cout << param.time_label << " - unsupported event #" << param.code << "." << std::endl;
                }
            }

            for (const std::string& filename : m_allocated_sectFiles)
                if (::remove(filename.c_str()))
                    std::cout << "Failed to cleanup diff storage file \"" << filename << "\". " << std::strerror(errno)
                              << std::endl;
        }
        catch (std::exception& ex)
        {
            std::cerr << ex.what() << std::endl;
            throw std::runtime_error("Stretch snapshot service failed.");
        }
        std::cout << "Stretch snapshot service finished." << std::endl;
    };
};

#ifdef BLKSNAP_MODIFICATION
static int CalculateTzMinutesWest()
{
    time_t local_time = time(NULL);

    struct tm *gm_tm = gmtime(&local_time);
    gm_tm->tm_isdst = -1;
    time_t gm_time = mktime(gm_tm);

    return static_cast<int>(difftime(local_time, gm_time) / 60);
}

static inline int getTzMinutesWest()
{
    static int tzMinutesWest = -1;

    if (tzMinutesWest != -1)
        return tzMinutesWest;
    else
        return (tzMinutesWest = CalculateTzMinutesWest());
}

class SetlogArgsProc : public IArgsProc
{
public:
    SetlogArgsProc()
        : IArgsProc()
    {
        m_usage = std::string("Set module additional logging.");
        m_desc.add_options()
          ("level,l", po::value<int>()->default_value(6), "3 - only errors, 4 - warnings, 5 - notice, 6 - info (default), 7 - debug.")
          ("path,p", po::value<std::string>(), "Full path for log file.")
          ("disable", "Disable additional logging.");
    };
    void Execute(po::variables_map& vm) override
    {
        CBlksnapFileWrap blksnapFd;
        struct blksnap_setlog param = {0};

        if (vm.count("disable")) {
            param.level = -1;
            param.filepath = NULL;
            if (::ioctl(blksnapFd.get(), IOCTL_BLKSNAP_SETLOG, &param))
                throw std::system_error(errno, std::generic_category(), "Failed to disable logging.");
            return;
        }

        param.tz_minuteswest = getTzMinutesWest();
        param.level = vm["level"].as<int>();

        if (!vm.count("path"))
            throw std::invalid_argument("Argument 'path' is missed.");

        std::string path = vm["path"].as<std::string>();

        std::vector<__u8> filepathBuffer(path.size());
        memcpy(filepathBuffer.data(), path.c_str(), path.size());

        param.filepath = filepathBuffer.data();
        param.filepath_size = filepathBuffer.size();

        if (::ioctl(blksnapFd.get(), IOCTL_BLKSNAP_SETLOG, &param))
            throw std::system_error(errno, std::generic_category(), "Failed to set logging.");
    };
};
#endif

static std::map<std::string, std::shared_ptr<IArgsProc>> argsProcMap{
  {"version", std::make_shared<VersionArgsProc>()},
  {"attach", std::make_shared<AttachArgsProc>()},
  {"detach", std::make_shared<DetachArgsProc>()},
  {"cbtinfo", std::make_shared<CbtInfoArgsProc>()},
  {"readcbtmap", std::make_shared<ReadCbtMapArgsProc>()},
  {"markdirtyblock", std::make_shared<MarkDirtyBlockArgsProc>()},
  {"snapshot_info", std::make_shared<SnapshotInfoArgsProc>()},
  {"snapshot_add", std::make_shared<SnapshotAddArgsProc>()},
  {"snapshot_create", std::make_shared<SnapshotCreateArgsProc>()},
  {"snapshot_destroy", std::make_shared<SnapshotDestroyArgsProc>()},
  {"snapshot_appendstorage", std::make_shared<SnapshotAppendStorageArgsProc>()},
  {"snapshot_take", std::make_shared<SnapshotTakeArgsProc>()},
  {"snapshot_waitevent", std::make_shared<SnapshotWaitEventArgsProc>()},
  {"snapshot_collect", std::make_shared<SnapshotCollectArgsProc>()},
  {"stretch_snapshot", std::make_shared<StretchSnapshotArgsProc>()},
#ifdef BLKSNAP_MODIFICATION
  {"setlog", std::make_shared<SetlogArgsProc>()},
#endif
};

static void printUsage()
{
    std::cout << "Usage:" << std::endl;
    std::cout << "--help, -h or help:" << std::endl;
    std::cout << "\tPrint this usage." << std::endl;
    std::cout << "<command> [arguments]:" << std::endl;
    std::cout << "\tExecute the management command." << std::endl;
    std::cout << std::endl;
    std::cout << "Available commands with arguments:" << std::endl;
    for (const auto& it : argsProcMap)
    {
        std::cout << it.first << ":" << std::endl;
        it.second->PrintUsage();
    }
}

static void process(int argc, char** argv)
{
    if (argc < 2)
        throw std::runtime_error("Command not found.");

    std::string commandName(argv[1]);

    const auto& itArgsProc = argsProcMap.find(commandName);
    if (itArgsProc != argsProcMap.end())
        itArgsProc->second->Process(--argc, ++argv);
    else
        if ((commandName == "help") || (commandName == "--help") || (commandName == "-h"))
            printUsage();
        else
            throw std::runtime_error("Command is not set.");
}

int main(int argc, char* argv[])
{
    int ret = 0;

    try
    {
        process(argc, argv);
    }
    catch (std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
        ret = 1;
    }

    return ret;
}
