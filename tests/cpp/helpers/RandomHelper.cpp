#include <stdlib.h>
#include <sys/types.h>
#include "RandomHelper.h"

void CRandomHelper::GenerateBuffer(void* buffer, size_t length)
{
    size_t lcount = length >> 2;
    long *lbuf = static_cast<long *>(buffer);
    char *chbuf = static_cast<char *>(buffer);

    for (size_t offset = 0; offset < lcount; offset++)
        lbuf[offset] = ::random();

    for (size_t offset = (lcount << 2); offset < length; offset++)
        chbuf[offset] = static_cast<char>(::random() & 0xFF);
}

long CRandomHelper::GenerateLong()
{
    return ::random();
}
