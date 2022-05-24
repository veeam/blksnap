/*
 * Copyright (C) 2022 Veeam Software Group GmbH <https://www.veeam.com/contacts.html>
 *
 * This file is part of libblksnap
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Lesser Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <atomic>
#include <blksnap/Blksnap.h>
#include <blksnap/Session.h>
#include <boost/filesystem.hpp>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <linux/fiemap.h>
#include <linux/fs.h>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

namespace fs = boost::filesystem;

using namespace blksnap;

struct SSessionInfo
{
    struct blk_snap_dev_t original;
    struct blk_snap_dev_t image;
    std::string originalName;
    std::string imageName;
};

struct SRangeVectorPos
{
    size_t rangeInx;
    sector_t rangeOfs;

    SRangeVectorPos()
        : rangeInx(0)
        , rangeOfs(0)
    {};
};

struct SState
{
    std::atomic<bool> stop;
    std::string diffStorage;
    uuid_t id;
    std::mutex lock;
    std::list<std::string> errorMessage;
    std::vector<std::string> diffStorageFiles;

    std::vector<SRange> diffStorageRanges;
    int diffDeviceMajor;
    int diffDeviceMinor;
    SRangeVectorPos diffStoragePosition;
};

class CSession : public ISession
{
public:
    CSession(const std::vector<std::string>& devices, const std::string& diffStorage, const SStorageRanges& diffStorageRanges);
    ~CSession() override;

    std::string GetImageDevice(const std::string& original) override;
    std::string GetOriginalDevice(const std::string& image) override;
    bool GetError(std::string& errorMessage) override;

private:
    uuid_t m_id;
    std::vector<SSessionInfo> m_devices;

    std::shared_ptr<CBlksnap> m_ptrBlksnap;
    std::shared_ptr<SState> m_ptrState;
    std::shared_ptr<std::thread> m_ptrThread;
};

std::shared_ptr<ISession> ISession::Create(const std::vector<std::string>& devices, const std::string& diffStorage)
{
    SStorageRanges diffStorageRanges;

    return std::make_shared<CSession>(devices, diffStorage, diffStorageRanges);
}

std::shared_ptr<ISession> ISession::Create(const std::vector<std::string>& devices, const SStorageRanges& diffStorageRanges)
{
    std::string diffStorage;

    return std::make_shared<CSession>(devices, diffStorage, diffStorageRanges);
}

namespace
{
    static inline struct blk_snap_dev_t deviceByName(const std::string& name)
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

    static void FiemapStorage(const std::string& filename, struct blk_snap_dev_t& dev_id,
                              std::vector<struct blk_snap_block_range>& ranges)
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
        dev_id.mj = major(st.st_dev);
        dev_id.mn = minor(st.st_dev);

        fd = ::open(filename.c_str(), O_RDONLY | O_EXCL | O_LARGEFILE);
        if (fd < 0)
        {
            ret = errno;
            errMessage = "Failed to open file.";
            goto fail;
        }

        map = (struct fiemap*)::malloc(sizeof(struct fiemap) + sizeof(struct fiemap_extent) * extentMax);
        if (!map)
        {
            ret = ENOMEM;
            errMessage = "Failed to allocate memory for fiemap structure.";
            goto fail;
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
                struct blk_snap_block_range rg;
                struct fiemap_extent* extent = map->fm_extents + i;

                if (extent->fe_physical & (SECTOR_SIZE - 1))
                {
                    ret = EINVAL;
                    errMessage = "File location is not ordered by sector size.";
                    goto out;
                }

                rg.sector_offset = extent->fe_physical >> SECTOR_SHIFT;
                rg.sector_count = extent->fe_length >> SECTOR_SHIFT;
                ranges.push_back(rg);

                fileOffset = extent->fe_logical + extent->fe_length;

                // std::cout << "allocate range: ofs=" << rg.sector_offset << " cnt=" << rg.sector_count << std::endl;
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

    static void FallocateStorage(const std::string& filename, const off_t filesize)
    {
        int fd = ::open(filename.c_str(), O_CREAT | O_RDWR | O_EXCL | O_LARGEFILE);
        if (fd < 0)
            throw std::system_error(errno, std::generic_category(), "[TBD]Failed to create file for diff storage.");

        if (::fallocate64(fd, 0, 0, filesize))
        {
            int err = errno;

            ::remove(filename.c_str());
            ::close(fd);
            throw std::system_error(err, std::generic_category(), "[TBD]Failed to allocate file for diff storage.");
        }
        ::close(fd);
    }

    static void DeviceNumberByName(const std::string& deviceName, int& mj, int& mn)
    {
        struct stat64 st;

        if (::stat64(deviceName.c_str(), &st))
            throw std::system_error(errno, std::generic_category(), "Failed to get file size.");

        mj = major(st.st_rdev);
        mn = minor(st.st_rdev);
    }

    static void AllocateDiffStorage(std::shared_ptr<SState> ptrState, sector_t requestedSectors,
                                    struct blk_snap_dev_t& dev_id, std::vector<struct blk_snap_block_range>& ranges)
    {
        if (ptrState->diffStorageRanges.size() <= ptrState->diffStoragePosition.rangeInx)
            throw std::runtime_error("Failed to allocate diff storage. Not enough free ranges");

        dev_id.mj = ptrState->diffDeviceMajor;
        dev_id.mn = ptrState->diffDeviceMinor;

        while (requestedSectors)
        {
            const SRange& rg = ptrState->diffStorageRanges[ptrState->diffStoragePosition.rangeInx];

            sector_t ofs = rg.sector + ptrState->diffStoragePosition.rangeOfs;
            sector_t sz = std::min(rg.count - ptrState->diffStoragePosition.rangeOfs, requestedSectors);

            if (sz == 0)
            {
                ptrState->diffStoragePosition.rangeOfs = 0;
                if (ptrState->diffStorageRanges.size() == ++ptrState->diffStoragePosition.rangeInx)
                    break;

                continue;
            }

            {
                struct blk_snap_block_range rg = {
                    .sector_offset = ofs,
                    .sector_count = sz
                };

                ranges.push_back(rg);
            }

            ptrState->diffStoragePosition.rangeOfs += sz;
            requestedSectors -= sz;
        }
    }
    static void LogAppendedRanges(std::vector<struct blk_snap_block_range>& ranges)
    {
        sector_t totalSectors = 0;

        std::cout << "" << std::endl;
        std::cout << "Append " << ranges.size() << " ranges: " << std::endl;
        for (const struct blk_snap_block_range& rg : ranges)
        {
            totalSectors += rg.sector_count;
            std::cout << std::to_string(rg.sector_offset) << ":"<< std::to_string(rg.sector_count) << std::endl;

        }
        std::cout << "Total sectors append: " << totalSectors << std::endl;

    }
} //

static void BlksnapThread(std::shared_ptr<CBlksnap> ptrBlksnap, std::shared_ptr<SState> ptrState)
{
    struct SBlksnapEvent ev;
    int diffStorageNumber = 1;
    bool is_eventReady;

    while (!ptrState->stop)
    {
        try
        {
            is_eventReady = ptrBlksnap->WaitEvent(ptrState->id, 100, ev);
        }
        catch (std::exception& ex)
        {
            std::cerr << ex.what() << std::endl;
            std::lock_guard<std::mutex> guard(ptrState->lock);
            ptrState->errorMessage.push_back(std::string(ex.what()));
            break;
        }

        if (!is_eventReady)
            continue;

        try
        {
            switch (ev.code)
            {
            case blk_snap_event_code_low_free_space:
            {
                struct blk_snap_dev_t dev_id;
                std::vector<struct blk_snap_block_range> ranges;

                if (!ptrState->diffStorage.empty())
                {
                    fs::path filepath(ptrState->diffStorage);
                    filepath += std::string("diff_storage#" + std::to_string(diffStorageNumber++));
                    if (fs::exists(filepath))
                        fs::remove(filepath);
                    std::string filename = filepath.string();

                    {
                        std::lock_guard<std::mutex> guard(ptrState->lock);
                        ptrState->diffStorageFiles.push_back(filename);
                    }
                    FallocateStorage(filename, ev.lowFreeSpace.requestedSectors << SECTOR_SHIFT);
                    FiemapStorage(filename, dev_id, ranges);
                }
                else
                    AllocateDiffStorage(ptrState, ev.lowFreeSpace.requestedSectors, dev_id, ranges);

                LogAppendedRanges(ranges);
                ptrBlksnap->AppendDiffStorage(ptrState->id, dev_id, ranges);
            }
            break;
            case blk_snap_event_code_corrupted:
                throw std::system_error(ev.corrupted.errorCode, std::generic_category(),
                                        std::string("Snapshot corrupted for device "
                                                    + std::to_string(ev.corrupted.origDevId.mj) + ":"
                                                    + std::to_string(ev.corrupted.origDevId.mn)));
                break;
            default:
                throw std::runtime_error("Invalid blksnap event code received.");
            }
        }
        catch (std::exception& ex)
        {
            std::cerr << ex.what() << std::endl;
            std::lock_guard<std::mutex> guard(ptrState->lock);
            ptrState->errorMessage.push_back(std::string(ex.what()));
        }
    }
}

CSession::CSession(const std::vector<std::string>& devices, const std::string& diffStorage, const SStorageRanges& diffStorageRanges)
{
    m_ptrBlksnap = std::make_shared<CBlksnap>();

    for (const auto& name : devices)
    {
        SSessionInfo info;

        info.originalName = name;
        info.original = deviceByName(name);
        info.image = {0};
        info.imageName = "";
        m_devices.push_back(info);
    }

    /*
     * Create snapshot
     */
    std::vector<struct blk_snap_dev_t> blk_snap_devs;
    for (const SSessionInfo& info : m_devices)
        blk_snap_devs.push_back(info.original);
    m_ptrBlksnap->Create(blk_snap_devs, m_id);

    /*
     * Prepare state structure for thread
     */
    m_ptrState = std::make_shared<SState>();
    m_ptrState->stop = false;
    if (!diffStorage.empty())
        m_ptrState->diffStorage = diffStorage;
    if (!diffStorageRanges.ranges.empty())
        m_ptrState->diffStorageRanges = diffStorageRanges.ranges;
    if (!diffStorageRanges.device.empty())
        DeviceNumberByName(diffStorageRanges.device, m_ptrState->diffDeviceMajor, m_ptrState->diffDeviceMinor);
    uuid_copy(m_ptrState->id, m_id);

    /*
     * Append first portion for diff storage
     */
    struct SBlksnapEvent ev;
    if (m_ptrBlksnap->WaitEvent(m_id, 100, ev))
    {
        switch (ev.code)
        {
        case blk_snap_event_code_low_free_space:
        {
            struct blk_snap_dev_t dev_id;
            std::vector<struct blk_snap_block_range> ranges;

            if (!m_ptrState->diffStorage.empty())
            {
                fs::path filepath(m_ptrState->diffStorage);
                filepath += std::string("diff_storage#" + std::to_string(0));
                if (fs::exists(filepath))
                    fs::remove(filepath);
                std::string filename = filepath.string();

                m_ptrState->diffStorageFiles.push_back(filename);
                FallocateStorage(filename, ev.lowFreeSpace.requestedSectors << SECTOR_SHIFT);
                FiemapStorage(filename, dev_id, ranges);
            }
            else
                AllocateDiffStorage(m_ptrState, ev.lowFreeSpace.requestedSectors, dev_id, ranges);

            LogAppendedRanges(ranges);
            m_ptrBlksnap->AppendDiffStorage(m_id, dev_id, ranges);
        }
        break;
        case blk_snap_event_code_corrupted:
            throw std::system_error(ev.corrupted.errorCode, std::generic_category(),
                                    std::string("Failed to create snapshot for device "
                                                + std::to_string(ev.corrupted.origDevId.mj) + ":"
                                                + std::to_string(ev.corrupted.origDevId.mn)));
            break;
        default:
            throw std::runtime_error("Invalid blksnap event code received.");
        }
    }

    /*
     * Start stretch snapshot thread
     */
    m_ptrThread = std::make_shared<std::thread>(BlksnapThread, m_ptrBlksnap, m_ptrState);
    ::usleep(0);

    /*
     * Take snapshot
     */
    m_ptrBlksnap->Take(m_id);

    /*
     * Collect images
     */
    std::vector<struct blk_snap_image_info> images;
    m_ptrBlksnap->Collect(m_id, images);

    for (const struct blk_snap_image_info& imageInfo : images)
    {
        for (size_t inx = 0; inx < m_devices.size(); inx++)
        {
            if ((m_devices[inx].original.mj == imageInfo.orig_dev_id.mj)
                && (m_devices[inx].original.mn == imageInfo.orig_dev_id.mn))
            {
                m_devices[inx].image = imageInfo.image_dev_id;
                m_devices[inx].imageName
                  = std::string("/dev/" BLK_SNAP_IMAGE_NAME) + std::to_string(imageInfo.image_dev_id.mn);
            }
        }
    }
}

CSession::~CSession()
{
    // std::cout << "Destroy blksnap session" << std::endl;
    /**
     * Stop thread
     */
    m_ptrState->stop = true;
    m_ptrThread->join();

    /**
     * Destroy snapshot
     */
    try
    {
        m_ptrBlksnap->Destroy(m_id);
    }
    catch (std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
        return;
    }

    /**
     * Cleanup diff storage files
     */
    for (const std::string& filename : m_ptrState->diffStorageFiles)
    {
        try
        {
            if (::remove(filename.c_str()))
                throw std::system_error(errno, std::generic_category(), "[TBD]Failed to remove diff storage file.");
        }
        catch (std::exception& ex)
        {
            std::cerr << ex.what() << std::endl;
        }
    }
}

std::string CSession::GetImageDevice(const std::string& original)
{
    struct blk_snap_dev_t devId = deviceByName(original);

    for (size_t inx = 0; inx < m_devices.size(); inx++)
    {
        if ((m_devices[inx].original.mj == devId.mj) && (m_devices[inx].original.mn == devId.mn))
            return m_devices[inx].imageName;
    }

    throw std::runtime_error("Failed to get image device for [" + original + "].");
}

std::string CSession::GetOriginalDevice(const std::string& image)
{
    struct blk_snap_dev_t devId = deviceByName(image);

    for (size_t inx = 0; inx < m_devices.size(); inx++)
    {
        if ((m_devices[inx].image.mj == devId.mj) && (m_devices[inx].image.mn == devId.mn))
            return m_devices[inx].originalName;
    }

    throw std::runtime_error("Failed to get original device for [" + image + "].");
}

bool CSession::GetError(std::string& errorMessage)
{
    std::lock_guard<std::mutex> guard(m_ptrState->lock);
    if (!m_ptrState->errorMessage.size())
        return false;

    errorMessage = m_ptrState->errorMessage.front();
    m_ptrState->errorMessage.pop_front();
    return true;
}
