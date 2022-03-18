// SPDX-License-Identifier: GPL-2.0+
#include <algorithm>
#include <cstdlib>
#include <blksnap/Service.h>
#include <blksnap/Session.h>
#include <boost/program_options.hpp>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "helpers/AlignedBuffer.hpp"
#include "helpers/BlockDevice.h"
#include "helpers/Log.h"
#include "helpers/RandomHelper.h"
#include "TestSector.h"

namespace po = boost::program_options;
using blksnap::sector_t;
using blksnap::SRange;

void CheckPerformance(const std::string& device, const std::string& diffStorage)
{
    bool isErrorFound = false;

    logger.Info("--- Test: check performance ---");

    throw std::runtime_error("Is not implemented yet.");

    if (isErrorFound)
        throw std::runtime_error("--- Failed: check performance ---");

    logger.Info("--- Success: check performance ---");
}

void Main(int argc, char* argv[])
{
    po::options_description desc;
    std::string usage = std::string("Checking the performance of the COW algorithm of the blksnap module.");

    desc.add_options()
        ("help,h", "Show usage information.")
        ("log,l", po::value<std::string>(),"Detailed log of all transactions.")
        ("device,d", po::value<std::string>(), "Device name. ")
        ("diff_storage,s", po::value<std::string>(),
            "Directory name for allocating diff storage files.");
    po::variables_map vm;
    po::parsed_options parsed = po::command_line_parser(argc, argv).options(desc).run();
    po::store(parsed, vm);
    po::notify(vm);

    if (vm.count("help"))
    {
        std::cout << usage << std::endl;
        std::cout << desc << std::endl;
        return;
    }

    if (vm.count("log"))
    {
        std::string filename = vm["log"].as<std::string>();
        logger.Open(filename);
    }

    if (!vm.count("device"))
        throw std::invalid_argument("Argument 'device' is missed.");
    std::string origDevName = vm["device"].as<std::string>();

    if (!vm.count("diff_storage"))
        throw std::invalid_argument("Argument 'diff_storage' is missed.");
    std::string diffStorage = vm["diff_storage"].as<std::string>();

    std::srand(std::time(0));
    CheckPerformance(origDevName, diffStorage);
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
