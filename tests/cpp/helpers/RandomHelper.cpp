#include "RandomHelper.h"
#include <stdlib.h>

void CRandomHelper::GenerateBuffer(void* buffer, size_t length)
{
	size_t lcount = length >> 2;
	long *lbuf = buffer;
	char *chbuf = buffer;

	for (size_t offset = 0; offset < lcount; offset++)
		lbuf[offset] = ::random();

	for (size_t offset = (lcount << 2); offset < length; offset++)
		chbuf[offset] = static_cast<char>(::random() & 0xFF);
}
