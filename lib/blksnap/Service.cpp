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
#include <blksnap/SnapshotCtl.h>
#include <blksnap/Service.h>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>


/**
 * The version is displayed as a string for informational purposes only.
 * When new module features appear, they must be supported in the library.
 */
std::string blksnap::Version()
{
    struct blksnap_version version;

    CSnapshotCtl::Version(version);

    std::stringstream ss;
    ss << version.major << "." << version.minor << "." << version.revision << "." << version.build;

    return ss.str();
}
