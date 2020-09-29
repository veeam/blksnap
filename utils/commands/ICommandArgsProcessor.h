#ifndef BLK_SNAP_ICOMMANDARGSPROCESSOR_H
#define BLK_SNAP_ICOMMANDARGSPROCESSOR_H

#include <string>
#include <vector>
#include <memory>

class ICommandArgsProcessor
{
public:
    using Ptr = std::shared_ptr<ICommandArgsProcessor>;

    virtual std::string GetCommandName() = 0;
    virtual std::string Process(std::vector<std::string> args) = 0;
    virtual std::string GetHelpMessage() = 0;
};

#endif // BLK_SNAP_ICOMMANDARGSPROCESSOR_H
