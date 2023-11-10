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

struct SState
{
    std::atomic<bool> stop;
    std::string diffStorage;
    std::mutex lock;
    std::list<std::string> errorMessage;
};

class CSession : public ISession
{
public:
    CSession(const std::vector<std::string>& devices,
             const std::string& diffStorageFilePath,
             const unsigned long long limit);
    ~CSession() override;

    bool GetError(std::string& errorMessage) override;

private:
    CSnapshotId m_id;

    std::shared_ptr<CSnapshotCtl> m_ptrCtl;
    std::shared_ptr<SState> m_ptrState;
    std::shared_ptr<std::thread> m_ptrThread;
};

std::shared_ptr<ISession> ISession::Create(
    const std::vector<std::string>& devices,
    const std::string& diffStorageFilePath,
    const unsigned long long limit)
{
    return std::make_shared<CSession>(devices, diffStorageFilePath, limit);
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
            is_eventReady = ptrCtl->WaitEvent(100, ev);
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
            case blksnap_event_code_corrupted:
                throw std::system_error(ev.corrupted.errorCode, std::generic_category(),
                    std::string("Snapshot corrupted for device " + std::to_string(ev.corrupted.origDevIdMj) + ":" + std::to_string(ev.corrupted.origDevIdMn)));
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

CSession::CSession(const std::vector<std::string>& devices, const std::string& diffStorageFilePath, const unsigned long long limit)
{
    for (const auto& name : devices)
        CTrackerCtl(name).Attach();

    // Create snapshot
    auto snapshot = CSnapshotCtl::Create(diffStorageFilePath, limit);

    // Add devices to snapshot
    for (const auto& name : devices)
        CTrackerCtl(name).SnapshotAdd(snapshot->Id());

    // Prepare state structure for thread
    m_ptrState = std::make_shared<SState>();
    m_ptrState->stop = false;

    // Append first portion for diff storage
    struct SBlksnapEvent ev;
    if (snapshot->WaitEvent(100, ev))
    {
        switch (ev.code)
        {
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
    snapshot->Take();
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
        m_ptrCtl->Destroy();
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
