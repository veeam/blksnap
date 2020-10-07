#pragma once

#include <boost/filesystem.hpp>

class FileHelper
{
public:
    static std::string CalcHash(boost::filesystem::path file);
    static void Create(boost::filesystem::path file, size_t size);
    static void FillRandom(boost::filesystem::path file);
};
