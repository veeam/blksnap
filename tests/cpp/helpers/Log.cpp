#include "Log.h"
#include <ctime>
#include <thread>
#include <atomic>

void CLog::Open(const std::string &filename)
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

void CLog::Info(const std::string &message)
{
    std::lock_guard<std::mutex> guard(m_lock);

    std::cout << message << std::endl;
    if (m_isOpen)
        m_out << std::clock() << " " << std::this_thread::get_id() << " " << message << std::endl;
};

void CLog::Info(const std::stringstream &ss)
{
    std::lock_guard<std::mutex> guard(m_lock);

    std::cout << ss.str() << std::endl;
    if (m_isOpen)
        m_out << std::clock() << " " << std::this_thread::get_id() << " " << ss.str() << std::endl;
};

void CLog::Err(const char* message)
{
    std::lock_guard<std::mutex> guard(m_lock);

    std::cerr << message << std::endl;
    if (m_isOpen)
        m_out << std::clock() << std::this_thread::get_id() << " ERR " << " " << message << std::endl;
};

void CLog::Err(const std::string &message)
{
    std::lock_guard<std::mutex> guard(m_lock);

    std::cerr << message << std::endl;
    if (m_isOpen)
        m_out << std::clock() << std::this_thread::get_id() << " ERR " << " " << message << std::endl;
};

void CLog::Err(const  std::stringstream &ss)
{
    std::lock_guard<std::mutex> guard(m_lock);

    std::cerr << ss.str() << std::endl;
    if (m_isOpen)
        m_out << std::clock() << std::this_thread::get_id() << " ERR " << " " << ss.str() << std::endl;
};

void CLog::Detail(const char* message)
{
    std::lock_guard<std::mutex> guard(m_lock);

    if (m_isOpen)
        m_out << std::clock() << " " << std::this_thread::get_id() << " " << message << std::endl;
};

void CLog::Detail(const std::string &message)
{
    std::lock_guard<std::mutex> guard(m_lock);

    if (m_isOpen)
        m_out << std::clock() << " " << std::this_thread::get_id() << " " << message << std::endl;
};

void CLog::Detail(const std::stringstream &ss)
{
    std::lock_guard<std::mutex> guard(m_lock);

    if (m_isOpen)
        m_out << std::clock() << " " << std::this_thread::get_id() << " " <<    ss.str() << std::endl;
};

CLog logger;
