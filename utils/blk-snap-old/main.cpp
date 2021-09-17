#include <iostream>
#include "ArgsProcessor.h"

int main(int argc, char* argv[])
{
    try
    {
        ArgsProcessor args;
        return args.Run(argc, argv);
    }
    catch(std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
        return 1;
    }
}