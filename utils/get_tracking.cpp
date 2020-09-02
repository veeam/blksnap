#include <iostream>
#include <system_error>
#include <blksnap/snapshot_ctl.h>
#include <sys/stat.h>
#include <iomanip>

int main(int argc, char *argv[])
{
    struct snap_ctx* snapCtx;
    if (snap_ctx_create(&snapCtx) != 0)
        throw std::system_error(errno, std::generic_category(), "Failed to create snap context");

    //int snap_get_tracking(struct snap_ctx* ctx, struct cbt_info_s* cbtInfos, unsigned int* count)

    unsigned int length = 0;
    if (snap_get_tracking(snapCtx, NULL, &length) != 0)
        throw std::system_error(errno, std::generic_category(), "Failed to get tracking length");

    if (length == 0)
    {
        std::cout << "No tracking found" << std::endl;
        return 0;
    }

    struct cbt_info_s* cbtInfos = (struct cbt_info_s*)malloc(sizeof(struct cbt_info_s) * length);
    if (snap_get_tracking(snapCtx, cbtInfos, &length) != 0)
        throw std::system_error(errno, std::generic_category(), "Failed to get tracking");

    for (unsigned int i = 0; i < length; i++)
    {
        std::cout << "dev number (major:minor): " << cbtInfos[i].dev_id.major << ":" << cbtInfos[i].dev_id.minor << std::endl;
        std::cout << "snapNumber: " << (int)cbtInfos[i].snap_number << std::endl;
        std::cout << "cbt_map_size: " << cbtInfos[i].cbt_map_size << std::endl;

        std::cout << "GenerationId: ";
        std::ios_base::fmtflags f( std::cout.flags() );
        for (unsigned int j = 0; j < sizeof(cbt_info_s::generationId); ++j)
            std::cout << std::hex << std::setfill('0') << std::setw(2) << (int)cbtInfos[i].generationId[j];
        std::cout.flags( f );

        std::cout << std::endl;
        std::cout << std::endl;
    }

    free(cbtInfos);
}


