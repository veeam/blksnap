#pragma once

#include <unistd.h>

class FdHolder
{
public:
    FdHolder(int fd) : m_fd(fd) {};
    ~FdHolder() {::close(m_fd);}
    
    int Get() {return m_fd;}
    
private:
    int m_fd;
};
