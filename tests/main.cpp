#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>
#include <blk-snap/snapshot_ctl.h>

int main(int argc, char* argv[])
{
    
    set_snapshot_image_name("/dev/veeamsnap");
    Catch::Session sess;
    sess.applyCommandLine(argc, argv);
    
    int result = sess.run();
    return result;

};