#include <iostream>
#include "Service.h"
#include "Blksnap.h"

std::string blksnap::Version()
{
    CBlksnap blksnap;
    struct blk_snap_version version;

    blksnap.Version(version);

    std::stringstream ss;
    ss << version.major << "." << version.minor << "." << version.revision << "." << version.build;

    if (version.compatibility_flags)
        ss << "-0x" << std::hex << version.compatibility_flags << std::dec;

    std::string modification(param.mod_name);
    if (param.mod_name[0])
       ss << "-" << std::string(param.mod_name[0]);

    return ss.str();
}
