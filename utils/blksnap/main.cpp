#include <iostream>
#include <fstream> 
#include <vector>
#include <map>

#include <boost/program_options.hpp>
namespace po = boost::program_options;

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
#include "../../module/blk_snap.h"
static int blksnap_fd = 0;
static const char* blksnap_filename = "/dev/" MODULE_NAME;

static
dev_t deviceByName(const std::string &name)
{
    struct stat st;

    if (::stat(name.c_str(), &st))
        throw std::system_error(errno, std::generic_category(), name);

    return st.st_rdev;
}

static
void parseRanges(const std::string &str, std::vector<struct blk_snap_block_range> &ranges)
{
    size_t pos = 0;
    size_t valuePos = 0;
    struct blk_snap_block_range rg = {0};

    if (str.empty())
        throw std::invalid_argument("String should not be empty.");

    while (pos < str.size()) {
        char ch = str[pos];

        if (ch == ':') {
            rg.sector_offset = std::stoull(str.substr(valuePos, pos - valuePos));
            valuePos = pos;
        } else if (ch == ',') {
            rg.sector_count = std::stoull(str.substr(valuePos, pos - valuePos));
            ranges.push_back(rg);
            valuePos = pos;
        }
        pos++;
    }
    if ((pos - valuePos) > 1) {
        rg.sector_count = std::stoull(str.substr(valuePos, pos - valuePos));
        ranges.push_back(rg);
        valuePos = pos;
    }
}

class IArgsProc
{
public:
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

        return Execute(vm);
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
    {
        m_usage = std::string("[TBD]Print " MODULE_NAME " module version.");
        m_desc.add_options()
            ("compatibility,c", "[TBD]Print only compatibility flag value in decimal form.")
            ("modification,m", "[TBD]Print only module modification name.")
            ("json,j", po::value<std::string>(), "[TBD]Use json format for output.");
    };

    void Execute(po::variables_map &vm) override
    {
        struct blk_snap_version param;

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

        std::cout << param.major << "." <<
                     param.minor << "." <<
                     param.revision << "." <<
                     param.build << std::endl;
    };
};

class TrackerRemoveArgsProc : public IArgsProc
{
public:
    TrackerRemoveArgsProc()
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

        auto deviceName = vm["device"].as<std::string>();
        param.dev_id = deviceByName(deviceName);
        if (::ioctl(blksnap_fd, IOCTL_BLK_SNAP_TRACKER_REMOVE, &param))
            throw std::system_error(errno, std::generic_category(),
                "Failed to remove block device from change tracking.");
    };
};

class TrackerCollectArgsProc : public IArgsProc
{
public:
    TrackerCollectArgsProc()
    {
        m_usage = std::string("[TBD]Collect block devices with change tracking.");
        m_desc.add_options()
            ("json,j", po::value<std::string>(), "[TBD]Use json format for output.");
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

        if (vm.count("json")) {
            std::cout << "json output is not suppoted yet." << std::endl;
        } else {
            char generationIdStr[64];

            for (const auto &it : cbtInfoVector) {
                uuid_unparse(it.generationId, generationIdStr);

                std::cout << "device=" << major(it.dev_id) << ":" << minor(it.dev_id) << std::endl;
                std::cout << "blk_size=" << it.blk_size << std::endl;
                std::cout << "device_capacity=" << it.device_capacity << std::endl;
                std::cout << "blk_count=" << it.blk_count << std::endl;
                std::cout << "generationId=" << generationIdStr << std::endl;
                std::cout << "snap_number=" << it.snap_number << std::endl;
                std::cout << std::endl;
            }
        }
    };
};

class TrackerReadCbtMapArgsProc : public IArgsProc
{
public:
    TrackerReadCbtMapArgsProc()
    {
        m_usage = std::string("[TBD]Read change tracking map.");
        m_desc.add_options()
            ("device,d", po::value<std::string>(), "[TBD]Device name.")
            ("file,f", po::value<std::string>(), "[TBD]File name for output.")
            ("json,j", po::value<std::string>(), "[TBD]Use json format for output.");
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
    {
        m_usage = std::string("[TBD]Mark blocks as changed in change tracking map.");
        m_desc.add_options()
            ("device,d", po::value<std::string>(), "[TBD]Device name.")
            ("ranges,r", po::value<std::string>(), "[TBD]Sectors ranges array in format 'sector:count,next_sector:next_count'.");
    };

    void Execute(po::variables_map &vm) override
    {
        struct blk_snap_tracker_mark_dirty_blocks param;
        std::vector<struct blk_snap_block_range> ranges;

        if (!vm.count("device"))
            throw std::invalid_argument("Argument 'device' is missed.");
        param.dev_id = deviceByName(vm["device"].as<std::string>());

        if (!vm.count("ranges"))
            throw std::invalid_argument("Argument 'ranges' is missed.");
        parseRanges(vm["ranges"].as<std::string>(), ranges);
        
        param.count = ranges.size();
        param.dirty_blocks_array = ranges.data();

        if (::ioctl(blksnap_fd, IOCTL_BLK_SNAP_TRACKER_MARK_DIRTY_BLOCKS, &param))
            throw std::system_error(errno, std::generic_category(),
                "[TBD]Failed to mark dirty blocks in change tracking map.");
    }
};

static
std::map<std::string, std::shared_ptr<IArgsProc> > argsProcMap {
    {"tracker_readcbtmap", std::make_shared<TrackerReadCbtMapArgsProc>()},
    {"tracker_collect", std::make_shared<TrackerCollectArgsProc>()},
    {"tracker_markdirtyblock", std::make_shared<TrackerMarkDirtyBlockArgsProc>()},
    {"tracker_remove", std::make_shared<TrackerRemoveArgsProc>()},
    {"version", std::make_shared<VersionArgsProc>()},

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
