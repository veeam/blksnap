/*  */
#pragma once
/*
 * The hi-level abstraction for the blksnap kernel module.
 * Allows to show module kernel version.
 */
#include <string>

namespace blksnap
{

std::string Version();

struct SectorState
{
    uint8_t snapNumberPrevious;
    uint8_t snapNumberCurrent;
    unsigned int chunkState;
};

void GetSectorState(const std::string &image, off_t offset, SectorState &state);

}
