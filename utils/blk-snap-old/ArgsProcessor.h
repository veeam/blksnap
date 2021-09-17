#ifndef BLK_SNAP_ARGSPROCESSOR_H
#define BLK_SNAP_ARGSPROCESSOR_H

#include <boost/program_options.hpp>
#include <vector>
#include <unordered_map>

#include "commands/ICommandArgsProcessor.h"

class ArgsProcessor
{
public:
    ArgsProcessor();
    ~ArgsProcessor() = default;

    int Run(int argc, char* argv[]);

private:
    int RunCommand(const std::string& command, const std::vector<std::string>& args);
    int PrintHelp(const std::string& command);

private:
    std::unordered_map<std::string, ICommandArgsProcessor::Ptr> m_commands;
};

#endif // BLK_SNAP_ARGSPROCESSOR_H
