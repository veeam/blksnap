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

#ifdef BOOST_CRC_HPP
        boost::crc_32_type calc;
        calc.process_bytes(buffer + offset + offsetof(STestHeader, seqNumber),
                           SECTOR_SIZE - offsetof(STestHeader, seqNumber));
        header->crc = calc.checksum();
#else
        header->crc = 'CC32';
#endif
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

#ifdef BOOST_CRC_HPP
        boost::crc_32_type calc;
        calc.process_bytes(buffer + offsetof(STestHeader, seqNumber),
                           SECTOR_SIZE - offsetof(STestHeader, seqNumber));
        crc = calc.checksum();
#else
        crc = 'CC32';
#endif

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
