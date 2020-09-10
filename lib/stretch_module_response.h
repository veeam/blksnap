#ifndef BLK_SNAP_STRETCH_MODULE_RESPONSE_H
#define BLK_SNAP_STRETCH_MODULE_RESPONSE_H

#include <stdint.h>

#pragma pack(push, 1)

struct stretch_response
{
    uint32_t cmd;
    uint8_t buffer[1020];
};

struct halfFill_response
{
    uint64_t FilledStatus;
};

struct overflow_response
{
    uint32_t errorCode;
    uint64_t filledStatus;
};

struct terminate_response
{
    uint64_t filledStatus;
};

#pragma pack(pop)

#endif //BLK_SNAP_STRETCH_MODULE_RESPONSE_H
