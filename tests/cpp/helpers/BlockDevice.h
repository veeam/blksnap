#include <string>
#include <sys/types.h>

class CBlockDevice
{
public:
    CBlockDevice(const std::string &name);
    ~CBlockDevice();

    void Read(void *buf, size_t count, off_t offset);
    void Write(const void *buf, size_t count, off_t offset);

    off_t Size();
    const std::string& Name();
private:
    std::string m_name;
    int m_fd;
};
