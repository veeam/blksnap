#include <string>
#include <sstream>
#include <iostream>
#include <fstream>
#include <mutex>

class CLog
{
public:
    CLog()
        : m_isOpen(false)
    {};
    ~CLog()
    {};

    void Open(const std::string &filename);

    void Info(const char* message);
    void Info(const std::string &message);
    void Info(const std::stringstream &ss);

    void Err(const char* message);
    void Err(const std::string &message);
    void Err(const std::stringstream &ss);

    void Detail(const char* message);
    void Detail(const std::string &message);
    void Detail(const std::stringstream &ss);
private:
    std::mutex m_lock;
    bool m_isOpen;
    std::ofstream m_out;
};

extern CLog logger;
