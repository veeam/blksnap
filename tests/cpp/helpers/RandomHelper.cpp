// SPDX-License-Identifier: GPL-2.0+
#include "RandomHelper.h"

#include <stdlib.h>
#include <sys/types.h>

void CRandomHelper::GenerateBuffer(void* buffer, size_t size)
{
    size_t icount = size / sizeof(int);
    int* ibuf = static_cast<int*>(buffer);
    char* chbuf = static_cast<char*>(buffer);
    int rnd = ::random();

    for (size_t offset = 0; offset < icount; offset++)
        ibuf[offset] = static_cast<int>(offset * rnd);

    for (size_t offset = (icount * sizeof(int)); offset < size; offset++)
        chbuf[offset] = static_cast<char>((offset * rnd) & 0xFF);
}

int CRandomHelper::GenerateInt()
{
    return ::random();
}
