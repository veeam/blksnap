/*
 * Copyright (C) 2022 Veeam Software Group GmbH <https://www.veeam.com/contacts.html>
 *
 * This file is part of libblksnap
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Lesser Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <blksnap/OpenFileHolder.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
 #include <fcntl.h>
#include <sys/types.h>
#include <system_error>

using namespace blksnap;

COpenFileHolder::COpenFileHolder(const std::string& filename, int flags, int mode/* = 0 */)
{
    int fd = mode ? ::open(filename.c_str(), flags, mode) : ::open(filename.c_str(), flags);
    if (fd < 0)
        throw std::system_error(errno, std::generic_category(),
            "Cannot open file [" + filename + "]");
    m_fd = fd;
};
COpenFileHolder::~COpenFileHolder()
{
    ::close(m_fd);
};

int COpenFileHolder::Get()
{
    return m_fd;
};
