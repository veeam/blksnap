// SPDX-License-Identifier: GPL-2.0+
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

class CLog
{
public:
    CLog()
        : m_isOpen(false){};
    ~CLog(){};

    void Open(const std::string& filename);

    void Info(const char* message);
    void Info(const std::string& message);
    void Info(const std::stringstream& ss);
    void Info(const void* buf, const size_t size);

    void Err(const char* message);
    void Err(const std::string& message);
    void Err(const std::stringstream& ss);
    void Err(const void* buf, const size_t size);

    void Detail(const char* message);
    void Detail(const std::string& message);
    void Detail(const std::stringstream& ss);
    void Detail(const void* buf, const size_t size);

private:
    std::mutex m_lock;
    bool m_isOpen;
    std::ofstream m_out;
};

extern CLog logger;
