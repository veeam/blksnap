#include "InMemoryStoreArgs.h"

#include <boost/program_options.hpp>
#include <iostream>

#include "BlkSnapCtx.h"
#include "BlkSnapStoreCtx.h"
#include "Helper.h"

namespace po = boost::program_options;

InMemoryStoreArgs::InMemoryStoreArgs()
{}

std::string InMemoryStoreArgs::GetCommandName()
{
    return "create-in-memory-store";
}

int InMemoryStoreArgs::Process(std::vector<std::string> args)
{
    po::options_description desc = CreateDesc();

    po::variables_map vm;
    po::parsed_options parsed = po::command_line_parser(args).options(desc).run();
    po::store(parsed, vm);
    po::notify(vm);

    BlkSnapCtx::Ptr ptrSnapCtx = std::make_shared<BlkSnapCtx>();

    std::vector<dev_t> snapDevs;
    snapDevs.push_back(Helper::GetDevice(vm["snap-dev"].as<std::string>()));

    BlkSnapStoreCtx storeCtx = BlkSnapStoreCtx::CreateInMemory(ptrSnapCtx, vm["size"].as<size_t>(), snapDevs);

    std::cout << storeCtx.GetUuid().ToStr() << std::endl;
    storeCtx.Detach();

    return 0;
}

boost::program_options::options_description InMemoryStoreArgs::CreateDesc()
{
    boost::program_options::options_description desc("[TBD] Create in memory snapshot store");

    // clang-format off
    desc.add_options()("size", po::value<std::uint64_t>()->default_value(500 * 1024 * 1024),"size")
                      ("snap-dev", po::value<std::string>()->required(), "device");
    // clang-format on

    return desc;
}

std::string InMemoryStoreArgs::GetHelpMessage()
{
    std::stringstream ss;
    ss << CreateDesc();
    return ss.str();
}
