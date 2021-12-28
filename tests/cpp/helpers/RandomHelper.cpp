#include <stdlib.h>
#include <sys/types.h>
#include "RandomHelper.h"

void CRandomHelper::GenerateBuffer(void* buffer, size_t size)
{
    size_t lcount = size / sizeof(long);
    long *lbuf = static_cast<long *>(buffer);
    char *chbuf = static_cast<char *>(buffer);

    for (size_t offset = 0; offset < lcount; offset++)
        lbuf[offset] = ::random();

    for (size_t offset = (lcount * sizeof(long)); offset < size; offset++)
        chbuf[offset] = static_cast<char>(::random() & 0xFF);
}

long CRandomHelper::GenerateLong()
{
    return ::random();
}
