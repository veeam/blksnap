/*
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
#include <string>
#include <sys/types.h>

class CBlockDevice
{
public:
    CBlockDevice(const std::string& name, const bool isSync = false, const off_t size = 0);
    ~CBlockDevice();

    void Read(void* buf, size_t count, off_t offset);
    void Write(const void* buf, size_t count, off_t offset);

    off_t Size();
    const std::string& Name();

private:
    std::string m_name;
    off_t m_size;
    int m_fd;
};
