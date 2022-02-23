/* [TBD]
 * Copyright (C) 2022 Veeam Software Group GmbH <https://www.veeam.com/contacts.html>
 *
 * This file is part of blksnap-tests
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

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

    static void Set(STestHeader* header, int inSeqNumber, blksnap::sector_t inSector);
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
    CTestSectorGenetor()
        : m_seqNumber(0)
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
    void Check(unsigned char* buffer, size_t size, blksnap::sector_t sector, const int seqNumber, const clock_t seqTime);


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
    std::atomic<int> m_seqNumber;
    int m_failCount;
    int m_logLineCount;
    std::vector<blksnap::SRange> m_failedRanges;
    bool m_isCrc32Checking;

private:
    void LogSector(blksnap::sector_t sector, const std::string& failMessage);
    void SetFailedSector(blksnap::sector_t sector, const std::string& failMessage);

};
