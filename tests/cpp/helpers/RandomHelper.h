// SPDX-License-Identifier: GPL-2.0+
#include <sys/types.h>

class CRandomHelper
{
public:
    static void GenerateBuffer(void* buffer, size_t size);
    static int GenerateInt();
};
