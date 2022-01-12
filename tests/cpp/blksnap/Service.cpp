#include <iostream>
#include <sstream>
#include "Service.h"
#include "Blksnap.h"

/**
 * The version is displayed as a string for informational purposes only.
 * When new module features appear, they must be supported in the library.
 */
std::string blksnap::Version()
{
    struct blk_snap_version version;

    CBlksnap blksnap;
    blksnap.Version(version);

    std::stringstream ss;
    ss << version.major << "." << version.minor << "." << version.revision << "." << version.build;

    if (version.compatibility_flags)
        ss << "-0x" << std::hex << version.compatibility_flags << std::dec;

    std::string modification;
    for (int inx = 0; inx < BLK_SNAP_MOD_NAME_LIMIT; inx++) {
        if (!version.mod_name[inx])
            break;
        modification += version.mod_name[inx];
    }
    if (!modification.empty())
        ss << "-" << modification;

    return ss.str();
}
