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

#include <blksnap/TrackerCtl.h>
#include <blksnap/SnapshotCtl.h>
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
    CSnapshotId id;
    std::mutex lock;
    std::list<std::string> errorMessage;

    std::vector<SRange> diffStorageRanges;
    std::string diffDevicePath;
    SRangeVectorPos diffStoragePosition;
};

class CSession : public ISession
{
public:
    CSession(const std::vector<std::string>& devices,
             const std::string& diffStorage);
    ~CSession() override;

    bool GetError(std::string& errorMessage) override;

private:
    CSnapshotId m_id;

    std::shared_ptr<CSnapshotCtl> m_ptrCtl;
    std::shared_ptr<SState> m_ptrState;
    std::shared_ptr<std::thread> m_ptrThread;
};

std::shared_ptr<ISession> ISession::Create(const std::vector<std::string>& devices, const std::string& diffStorage)
{
    return std::make_shared<CSession>(devices, diffStorage);
}

namespace
{
    static void FiemapStorage(const std::string& filename, std::string& devicePath,
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
        devicePath = "/dev/block/" +
            std::to_string(major(st.st_dev)) + ":" +
            std::to_string(minor(st.st_dev));

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

                // std::cout << "allocate range: ofs=" << rg.offset << " cnt=" << rg.count << std::endl;
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
        int fd = ::open(filename.c_str(), O_CREAT | O_RDWR | O_EXCL | O_LARGEFILE, 0600);
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
}

static void BlksnapThread(std::shared_ptr<CSnapshotCtl> ptrCtl, std::shared_ptr<SState> ptrState)
{
    struct SBlksnapEvent ev;
    int diffStorageNumber = 1;
    bool is_eventReady;

    while (!ptrState->stop)
    {
        try
        {
            is_eventReady = ptrCtl->WaitEvent(ptrState->id, 100, ev);
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
            case blksnap_event_code_low_free_space:
            {
                fs::path filepath(ptrState->diffStorage);
                filepath += std::string("diff_storage#" + std::to_string(diffStorageNumber++));
                if (fs::exists(filepath))
                    fs::remove(filepath);
                std::string filename = filepath.string();

                FallocateStorage(filename, ev.lowFreeSpace.requestedSectors << SECTOR_SHIFT);
                ptrCtl->AppendDiffStorage(ptrState->id, filename);
            }
            break;
            case blksnap_event_code_corrupted:
                throw std::system_error(ev.corrupted.errorCode, std::generic_category(),
                                        std::string("Snapshot corrupted for device " +
                                                    std::to_string(ev.corrupted.origDevIdMj) + ":" +
                                                    std::to_string(ev.corrupted.origDevIdMn)));
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

CSession::CSession(const std::vector<std::string>& devices, const std::string& diffStorage)
{
    m_ptrCtl = std::make_shared<CSnapshotCtl>();

    for (const auto& name : devices)
        CTrackerCtl(name).Attach();

    // Create snapshot
    m_id = m_ptrCtl->Create();

    // Add devices to snapshot
    for (const auto& name : devices)
        CTrackerCtl(name).SnapshotAdd(m_id.Get());

    // Prepare state structure for thread
    m_ptrState = std::make_shared<SState>();
    m_ptrState->stop = false;
    if (!diffStorage.empty())
        m_ptrState->diffStorage = diffStorage;
    m_ptrState->id = m_id;

    // Append first portion for diff storage
    struct SBlksnapEvent ev;
    if (m_ptrCtl->WaitEvent(m_id, 100, ev))
    {
        switch (ev.code)
        {
        case blksnap_event_code_low_free_space:
        {
            fs::path filepath(m_ptrState->diffStorage);
            filepath += std::string("diff_storage#" + std::to_string(0));
            if (fs::exists(filepath))
                fs::remove(filepath);
            std::string filename = filepath.string();

            FallocateStorage(filename, ev.lowFreeSpace.requestedSectors << SECTOR_SHIFT);
            m_ptrCtl->AppendDiffStorage(m_id, filename);
        }
        break;
        case blksnap_event_code_corrupted:
            throw std::system_error(ev.corrupted.errorCode, std::generic_category(),
                                    std::string("Failed to create snapshot for device "
                                                + std::to_string(ev.corrupted.origDevIdMj) + ":"
                                                + std::to_string(ev.corrupted.origDevIdMn)));
            break;
        default:
            throw std::runtime_error("Invalid blksnap event code received.");
        }
    }

    // Start stretch snapshot thread
    m_ptrThread = std::make_shared<std::thread>(BlksnapThread, m_ptrCtl, m_ptrState);
    ::usleep(0);


    // Take snapshot
    m_ptrCtl->Take(m_id);

}

CSession::~CSession()
{
    // std::cout << "Destroy blksnap session" << std::endl;

    // Stop thread
    m_ptrState->stop = true;
    m_ptrThread->join();

    // Destroy snapshot
    try
    {
        m_ptrCtl->Destroy(m_id);
    }
    catch (std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
        return;
    }
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
