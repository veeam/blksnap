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
#include <sys/stat.h>
#include <system_error>

struct SDeviceId
{
    unsigned int mj;
    unsigned int mn;

    SDeviceId()
        : mj(0)
        , mn(0){};

    SDeviceId(int mjr, int mnr)
        : mj(mjr)
        , mn(mnr){};

    std::string ToString() const
    {
        return std::to_string(mj) + ":" + std::to_string(mn);
    };

    bool operator==(const SDeviceId& devId) const
    {
        return (mj == devId.mj) && (mn == devId.mn);
    };

    bool operator!=(const SDeviceId& devId) const
    {
        return !operator==(devId);
    };

    bool operator<(const SDeviceId& devId) const
    {
        if (mj < devId.mj)
            return true;

        if (mj == devId.mj)
            if (mn < devId.mn)
                return true;

        return false;
    };

    static SDeviceId DeviceByName(const std::string& name)
    {
        struct stat st;

        if (::stat(name.c_str(), &st))
            throw std::system_error(errno, std::generic_category(), name);

        return SDeviceId(major(st.st_rdev), minor(st.st_rdev));
    };
};
