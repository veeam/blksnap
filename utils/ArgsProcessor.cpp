#include "ArgsProcessor.h"

#include <boost/program_options.hpp>
#include <iostream>

#include "commands/store/InMemoryStoreArgs.h"

ArgsProcessor::ArgsProcessor()
{}

int ArgsProcessor::Run(int argc, char** argv)
{
    namespace po = boost::program_options;

    po::options_description desc("Global options");

    // clang-format off
    boost::program_options::options_description commandDesc("");
    commandDesc.add_options()
      ("command", po::value<std::string>()->default_value(""), "command to execute")
      ("subargs", po::value<std::vector<std::string> >(), "Arguments for command");

    desc.add_options()
      ("help,h", "Show command help message");

    commandDesc.add(desc);
    // clang-format on

    po::positional_options_description pos;
    pos.add("command", 1).add("subargs", -1);

    po::variables_map vm;

    po::parsed_options parsed
      = po::command_line_parser(argc, argv).options(commandDesc).positional(pos).allow_unregistered().run();

    po::store(parsed, vm);

    bool isHelp = !vm["help"].empty();
    std::string command;
    if ( !vm["command"].empty() )
        command = vm["command"].as<std::string>();

    if ( command.empty() )
    {
        std::cout << desc << std::endl;
        return !isHelp;
    }

    if ( isHelp )
        return PrintHelp(command);

    std::vector<std::string> args = po::collect_unrecognized(parsed.options, po::include_positional);
    args.erase(args.begin());
    return RunCommand(command, args);
}

int ArgsProcessor::PrintHelp(const std::string& command)
{
    if ( m_commands.count(command) == 0 )
    {
        std::cerr << "Unknown command " << command << std::endl;
        return 1;
    }

    std::cout << m_commands[command]->GetHelpMessage() << std::endl;
    return 0;
}

int ArgsProcessor::RunCommand(const std::string& command, const std::vector<std::string>& args)
{
    try
    {
        if ( m_commands.count(command) == 0 )
        {
            std::cerr << "Unknown command " << command << std::endl;
            return 1;
        }

        m_commands[command]->Process(args);
        return 0;
    }
    catch(std::exception& ex)
    {
        std::cerr << "Failed to execute " << command << std::endl;
        std::cerr << ex.what() << command << std::endl;
        return 1;
    }
}
