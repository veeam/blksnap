// SPDX-License-Identifier: GPL-2.0+
#include <string>
#include <sys/types.h>

class CBlockDevice
{
public:
    CBlockDevice(const std::string& name, const bool isSync = false, const off_t size = 0);
    ~CBlockDevice();

    void Read(void* buf, size_t count, off_t offset);
    void Write(const void* buf, size_t count, off_t offset);

    off_t Size();
    size_t BlockSize();
    const std::string& Name();

private:
    std::string m_name;
    off_t m_size;
    int m_fd;
};
