// SPDX-License-Identifier: GPL-2.0+
#include <atomic>
#include <ctime>
#include <vector>
#include <blksnap/Sector.h>

struct STestHeader
{
    char head[8];
    int crc;
    int seqNumber;
    blksnap::sector_t sector;
    clock_t seqTime;

    void Init(int inSeqNumber, blksnap::sector_t inSector, const clock_t inSeqTime);
};

struct STestSector
{
    STestHeader header;
    char body[SECTOR_SIZE - sizeof(STestHeader)];
};

enum EFailType
{
    eFailCorruptedSector,
    eFailIncorrectSector,
};

class CTestSectorGenetor
{
public:
    CTestSectorGenetor(const bool useCrc32)
        : m_useCrc32(useCrc32)
        , m_seqNumber(0)
        , m_failCount(0)
        , m_logLineCount(0) {};
    ~CTestSectorGenetor() {};

    inline void IncSequence()
    {
        m_seqNumber++;
    };

    inline int GetSequenceNumber()
    {
        return m_seqNumber;
    };

    void Generate(unsigned char* buffer, size_t size, blksnap::sector_t sector);
    void Generate(unsigned char* buffer, size_t size, blksnap::sector_t sector, clock_t seqTime);
    void Check(unsigned char* buffer, size_t size, blksnap::sector_t sector, const int seqNumber, const clock_t seqTime, const bool isStrictly = false);


    inline int Fails()
    {
        return m_failCount;
    };
    /**
     * The function ShowFails() does not contain locks, since it should be
     * called when only one thread has access to used data.
     */
    inline const std::vector<blksnap::SRange>& GetFails()
    {
        return m_failedRanges;
    };

private:
    bool m_useCrc32;
    std::atomic<int> m_seqNumber;
    int m_failCount;
    int m_logLineCount;
    std::vector<blksnap::SRange> m_failedRanges;
    bool m_isCrc32Checking;

private:
    void LogSector(blksnap::sector_t sector, const std::string& failMessage);
    void SetFailedSector(blksnap::sector_t sector, const std::string& failMessage);

};
