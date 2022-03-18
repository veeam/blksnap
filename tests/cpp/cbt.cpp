// SPDX-License-Identifier: GPL-2.0+
#include <blksnap/Cbt.h>
#include <blksnap/Session.h>
#include <boost/program_options.hpp>
#include <iostream>

namespace po = boost::program_options;
using blksnap::sector_t;

void Main(int argc, char* argv[])
{
    std::runtime_error("It's not implemented yet.");
}

int main(int argc, char* argv[])
{
    try
    {
        Main(argc, argv);
    }
    catch (std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
