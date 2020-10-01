#ifndef BLK_SNAP_CREATESNAPSHOTARGS_H
#define BLK_SNAP_CREATESNAPSHOTARGS_H

#include <blk-snap/types.h>
#include <boost/program_options.hpp>
#include <cstdlib>
#include <vector>

#include "../ICommandArgsProcessor.h"

class CreateSnapshotArgs : public ICommandArgsProcessor
{
public:
    CreateSnapshotArgs();

    std::string GetCommandName() override;
    int Process(std::vector<std::string> args) override;
    std::string GetHelpMessage() override;

private:
    static boost::program_options::options_description CreateDesc();
};

#endif // BLK_SNAP_CREATESNAPSHOTARGS_H
