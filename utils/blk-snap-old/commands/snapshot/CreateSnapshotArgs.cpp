#include "CreateSnapshotArgs.h"

#include <boost/program_options.hpp>
#include <iostream>

#include "BlkSnapCtx.h"
#include "BlkSnapStoreCtx.h"
#include "Helper.h"
#include "Snapshot.h"

namespace po = boost::program_options;

CreateSnapshotArgs::CreateSnapshotArgs()
{}

std::string CreateSnapshotArgs::GetCommandName()
{
    return "create-snapshot";
}

int CreateSnapshotArgs::Process(std::vector<std::string> args)
{
    po::options_description desc = CreateDesc();

    po::variables_map vm;
    po::parsed_options parsed = po::command_line_parser(args).options(desc).run();
    po::store(parsed, vm);
    po::notify(vm);

    BlkSnapCtx::Ptr ptrSnapCtx = std::make_shared<BlkSnapCtx>();

    dev_t snapDev = Helper::GetDevice(vm["device"].as<std::string>());
    Uuid uuid = Uuid::Parse(vm["store"].as<std::string>());
    BlkSnapStoreCtx storeCtx = BlkSnapStoreCtx::Attach(ptrSnapCtx, uuid);
    Snapshot snapshot = Snapshot::Create(storeCtx, snapDev);

    std::cout << snapshot.GetId() << std::endl;
    snapshot.Detach();
    return 0;
}

boost::program_options::options_description CreateSnapshotArgs::CreateDesc()
{
    boost::program_options::options_description desc("[TBD] Create snapshot");

    // clang-format off
    desc.add_options()("store", po::value<std::string>()->required(), "store id")
                      ("device", po::value<std::string>()->required(), "device");
    // clang-format on

    return desc;
}

std::string CreateSnapshotArgs::GetHelpMessage()
{
    std::stringstream ss;
    ss << CreateDesc();
    return ss.str();
}
