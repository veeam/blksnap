// SPDX-License-Identifier: GPL-2.0+
#include <string>
#include <sys/types.h>

struct BlockRange {
    off_t offset;
    off_t count;

    BlockRange(const off_t inOffset, const off_t inCount)
        : offset(inOffset)
        , count(inCount)
    {};
};

class CAllocatedFile
{
public:
    CAllocatedFile(const std::string& name, const off_t size);
    ~CAllocatedFile();

    const std::string& Name();
	off_t Size();
    void Location(dev_t& dev_id, std::vector<BlockRange>& ranges);

private:
	std::string m_name;
    off_t size;
}
