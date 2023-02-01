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

    CSnapshotCtl ctl;
    ctl.Version(version);

    std::stringstream ss;
    ss << version.major << "." << version.minor << "." << version.revision << "." << version.build;

#ifdef BLKSNAP_MODIFICATION
    struct blksnap_mod mod;

    if (ctl.Modification(mod))
    {
        if (mod.compatibility_flags)
            ss << "-0x" << std::hex << mod.compatibility_flags << std::dec;

        std::string modification;
        for (int inx = 0; inx < BLKSNAP_MOD_NAME_LIMIT; inx++)
        {
            if (!mod.name[inx])
                break;
            modification += mod.name[inx];
        }
        if (!modification.empty())
            ss << "-" << modification;
    }
#endif
    return ss.str();
}

void blksnap::GetSectorState(const std::string& image, off_t offset, SectorState& state)
{
#ifdef BLKSNAP_MODIFICATION
    CSnapshotCtl ctl;
    struct blksnap_mod mod;

    if (!ctl.Modification(mod))
        throw std::runtime_error("Failed to get sector state. Modification is not supported in blksnap module");

#    ifdef BLKSNAP_DEBUG_SECTOR_STATE
    if (!(mod.compatibility_flags & (1 << blksnap_compat_flag_debug_sector_state)))
        throw std::runtime_error(
          "Failed to get sector state. Sectors state getting is not supported in blksnap module");

    struct blksnap_sector_state st = {0};
    ctl.GetSectorState(image, offset, st);

    state.snapNumberPrevious = st.snap_number_prev;
    state.snapNumberCurrent = st.snap_number_curr;
    state.chunkState = st.chunk_state;
#    else
    throw std::runtime_error("Failed to get sector state. It's not allowed");
#    endif
#else
    throw std::runtime_error("Failed to get sector state. It's not implemented");
#endif
}
