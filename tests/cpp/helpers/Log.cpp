// SPDX-License-Identifier: GPL-2.0+
#include "Log.h"

#include <atomic>
#include <ctime>
#include <iomanip>
#include <thread>

static void buf2stream(const void* buf, const size_t size, std::stringstream& ss)
{
    int inx = 0;

    ss << std::hex << std::setfill('0') ;
    do {
        unsigned int value = static_cast<const unsigned char*>(buf)[inx];

        ss << std::setw(2) << value << " ";

        inx++;

        if (!(inx % 16))
            ss << std::endl;
        else if (!(inx % 8))
            ss << " ";
    } while (inx < size);
};

void CLog::Open(const std::string& filename)
{
    std::lock_guard<std::mutex> guard(m_lock);

    m_out.open(filename, std::ios::trunc);
    m_isOpen = true;
};

void CLog::Info(const char* message)
{
    std::lock_guard<std::mutex> guard(m_lock);

    std::cout << message << std::endl;
    if (m_isOpen)
        m_out << std::clock() << " " << std::this_thread::get_id() << " " << message << std::endl;
};

void CLog::Info(const std::string& message)
{
    std::lock_guard<std::mutex> guard(m_lock);

    std::cout << message << std::endl;
    if (m_isOpen)
        m_out << std::clock() << " " << std::this_thread::get_id() << " " << message << std::endl;
};

void CLog::Info(const std::stringstream& ss)
{
    std::lock_guard<std::mutex> guard(m_lock);

    std::cout << ss.str() << std::endl;
    if (m_isOpen)
        m_out << std::clock() << " " << std::this_thread::get_id() << " " << ss.str() << std::endl;
};

void CLog::Info(const void* buf, const size_t size)
{
    std::stringstream ss;

    buf2stream(buf, size, ss);
    Info(ss);
}

void CLog::Err(const char* message)
{
    std::lock_guard<std::mutex> guard(m_lock);

    std::cerr << message << std::endl;
    if (m_isOpen)
        m_out << std::clock() << " " << std::this_thread::get_id() << " ERR " << message << std::endl;
};

void CLog::Err(const std::string& message)
{
    std::lock_guard<std::mutex> guard(m_lock);

    std::cerr << message << std::endl;
    if (m_isOpen)
        m_out << std::clock() << " " << std::this_thread::get_id() << " ERR " << message << std::endl;
};

void CLog::Err(const std::stringstream& ss)
{
    std::lock_guard<std::mutex> guard(m_lock);

    std::cerr << ss.str() << std::endl;
    if (m_isOpen)
        m_out << std::clock() << " " << std::this_thread::get_id() << " ERR " << ss.str() << std::endl;
};

void CLog::Err(const void* buf, const size_t size)
{
    std::stringstream ss;

    buf2stream(buf, size, ss);
    Err(ss);
}

void CLog::Detail(const char* message)
{
    std::lock_guard<std::mutex> guard(m_lock);

    if (m_isOpen)
        m_out << std::clock() << " " << std::this_thread::get_id() << " " << message << std::endl;
};

void CLog::Detail(const std::string& message)
{
    std::lock_guard<std::mutex> guard(m_lock);

    if (m_isOpen)
        m_out << std::clock() << " " << std::this_thread::get_id() << " " << message << std::endl;
};

void CLog::Detail(const std::stringstream& ss)
{
    std::lock_guard<std::mutex> guard(m_lock);

    if (m_isOpen)
        m_out << std::clock() << " " << std::this_thread::get_id() << " " << ss.str() << std::endl;
};

void CLog::Detail(const void* buf, const size_t size)
{
    std::stringstream ss;

    buf2stream(buf, size, ss);
    Detail(ss);
}

CLog logger;
