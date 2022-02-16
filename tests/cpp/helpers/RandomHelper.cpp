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
#include <stdlib.h>
#include <sys/types.h>
#include "RandomHelper.h"

void CRandomHelper::GenerateBuffer(void* buffer, size_t size)
{
    size_t icount = size / sizeof(int);
    int *ibuf = static_cast<int *>(buffer);
    char *chbuf = static_cast<char *>(buffer);
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
