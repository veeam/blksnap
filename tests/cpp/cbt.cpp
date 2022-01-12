#include <iostream>
#include "blksnap/Session.h"
#include "blksnap/Cbt.h"

#include <boost/program_options.hpp>
namespace po = boost::program_options;

#ifndef SECTOR_SHIFT
#define SECTOR_SHIFT 9
#endif
#ifndef SECTOR_SIZE
#define SECTOR_SIZE (1 << SECTOR_SHIFT)
#endif
typedef unsigned long long sector_t;


void Main(int argc, char *argv[])
{
    std::runtime_error("It's not implemented yet.");
}

int main(int argc, char *argv[])
{
    try
    {
        Main(argc, argv);
    }
    catch(std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
