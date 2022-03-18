// SPDX-License-Identifier: GPL-2.0+
#include <boost/crc.hpp>
#include <string.h>
#include "helpers/Log.h"
#include "helpers/RandomHelper.h"
#include "TestSector.h"

using blksnap::sector_t;
using blksnap::SRange;

const char* testHeadMagic = "testhead";

void STestHeader::Set(STestHeader* header, int inSeqNumber, sector_t inSector)
{
    header->seqNumber = inSeqNumber;
    header->sector = inSector;
    header->seqTime = std::clock();
    memcpy(header->head, testHeadMagic, 8);
};

void CTestSectorGenetor::Generate(unsigned char* buffer, size_t size, sector_t sector)
{
    for (size_t offset = 0; offset < size; offset += SECTOR_SIZE)
    {
        STestSector* current = (STestSector*)(buffer + offset);

        STestHeader* header = &current->header;
        STestHeader::Set(header, m_seqNumber, sector);

        CRandomHelper::GenerateBuffer(current->body, sizeof(current->body));
        // GenerateBuffer(current->body, sizeof(current->body), header);

        if (m_useCrc32)
        {
            boost::crc_32_type calc;
            calc.process_bytes(buffer + offset + offsetof(STestHeader, seqNumber),
                               SECTOR_SIZE - offsetof(STestHeader, seqNumber));
            header->crc = calc.checksum();
        }
        else
            header->crc = 0xDEC032CC;

        sector++;
    }
}

void CTestSectorGenetor::Check(unsigned char* buffer, size_t size, sector_t sector, const int seqNumber, const clock_t seqTime)
{
    for (size_t offset = 0; offset < size; offset += SECTOR_SIZE)
    {
        struct STestSector* current = (STestSector*)buffer;
        STestHeader* header = &current->header;
        int crc;

        if (m_useCrc32)
        {
            boost::crc_32_type calc;
            calc.process_bytes(buffer + offsetof(STestHeader, seqNumber),
                               SECTOR_SIZE - offsetof(STestHeader, seqNumber));
            crc = calc.checksum();
        }
        else
            crc = 0xDEC032CC;

        bool isCorrupted = (crc != header->crc);
        bool isIncorrect = (sector != header->sector);
        bool isInvalidSeqNumber = (header->seqNumber > seqNumber);
        bool isInvalidSeqTime = (header->seqTime > seqTime);

        if (isCorrupted || isIncorrect || isInvalidSeqNumber || isInvalidSeqTime)
        {
            std::string failMessage;

            if (m_logLineCount == 30)
                failMessage = "Too many sectors failed\n";
            else if (m_logLineCount < 30)
            {
                if (isCorrupted)
                {
                    failMessage += std::string("Corrupted sector\n");
                    failMessage += std::string("sector " + std::to_string(sector) + "\n");
                    failMessage
                      += std::string("crc " + std::to_string(header->crc) + " != " + std::to_string(crc) + "\n");
                }
                if (isIncorrect)
                {
                    failMessage += std::string("Incorrect sector\n");
                    failMessage += std::string("sector " + std::to_string(header->sector)
                                               + " != " + std::to_string(sector) + "\n");
                }
                if (isInvalidSeqNumber)
                {
                    failMessage += std::string("Invalid sequence number\n");
                    failMessage += std::string("sector " + std::to_string(header->sector) + "\n");
                    failMessage += std::string("seqNumber " + std::to_string(header->seqNumber) + " > "
                                               + std::to_string(seqNumber) + "\n");
                }
                if (isInvalidSeqTime)
                {
                    failMessage += std::string("Invalid sequence time\n");
                    failMessage += std::string("sector " + std::to_string(header->sector) + "\n");
                    failMessage += std::string("seqTime " + std::to_string(header->seqTime) + " > "
                                               + std::to_string(seqTime) + "\n");
                }
            }

            SetFailedSector(sector, failMessage);
            //logger.Err(buffer, 128);
        }

        sector++;
        buffer += SECTOR_SIZE;
    }
};

void CTestSectorGenetor::LogSector(sector_t sector, const std::string& failMessage)
{
    if (failMessage.empty())
        return;

    m_logLineCount++;
    logger.Err(failMessage);
}

void CTestSectorGenetor::SetFailedSector(sector_t sector, const std::string& failMessage)
{
    m_failCount++;

    if (!m_failedRanges.empty())
    {
        SRange& lastRange = m_failedRanges[m_failedRanges.size() - 1];
        if ((lastRange.sector + lastRange.count) == sector)
        {
            lastRange.count++;
            // LogSector(sector, failMessage);
            return;
        }
    }

    m_failedRanges.emplace_back(sector, 1);
    LogSector(sector, failMessage);
}
