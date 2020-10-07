#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

int main(int argc, char* argv[])
{
    Catch::Session sess;
    sess.applyCommandLine(argc, argv);
    
    int result = sess.run();
    return result;

};