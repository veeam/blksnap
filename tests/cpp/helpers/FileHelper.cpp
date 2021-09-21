#include "FileHelper.h"

#include <boost/algorithm/hex.hpp>
#include <fcntl.h>

#include "SHA256Calc.h"

#define BUFFER_SIZE 4096

std::string FileHelper::CalcHash(boost::filesystem::path file)
{
    int fd = -1;
    try
    {
        fd = ::open(file.c_str(), O_RDONLY);
        if ( fd == -1 )
            throw std::system_error(errno, std::generic_category(),
                                    std::string("Failed to open file: ") + file.string());

        SHA256Calc sha;
        char BUFFER[BUFFER_SIZE];
        int read = 0;
        while ( true )
        {
            read = ::read(fd, BUFFER, BUFFER_SIZE);
            if ( read == 0 )
                break;

            if ( read == -1 )
                throw std::system_error(errno, std::generic_category(),
                                        std::string("Failed to read data from file: ") + file.string());

            sha.Update(BUFFER, read);
        }

        unsigned char sha_hash[SHA256_DIGEST_LENGTH];
        sha.Final(sha_hash);

        std::string hex;
        boost::algorithm::hex(sha_hash, sha_hash + SHA256_DIGEST_LENGTH, back_inserter(hex));
    
        ::close(fd);
        return hex;
    }
    catch ( std::exception& ex )
    {
        if ( fd != -1 )
            ::close(fd);

        throw;
    }
}

void FileHelper::Create(boost::filesystem::path file, size_t size)
{
    int fd = -1;
    try
    {
        fd = open(file.c_str(), O_CREAT | O_EXCL | O_RDWR, S_IRWXU);
        if ( fd == -1 )
            throw std::system_error(errno, std::generic_category(), "Failed to create file");

        if ( ::fallocate64(fd, 0, 0, size) )
            throw std::system_error(errno, std::generic_category(), "Failed to allocate file space");
    
        ::close(fd);
    }
    catch ( std::exception& ex )
    {
        if ( fd != -1 )
            ::close(fd);

        throw;
    }
}

void FileHelper::FillRandom(boost::filesystem::path file)
{
    int fdRandom = -1;
    int fd = -1;
    try
    {
        fd = ::open(file.c_str(), O_RDWR);
        if ( fd == -1 )
            throw std::system_error(errno, std::generic_category(),
                                    std::string("Failed to open file: ") + file.string());

        fdRandom = ::open("/dev/urandom", O_RDONLY);
        if ( fdRandom == -1 )
            throw std::system_error(errno, std::generic_category(), "Failed to open file: /dev/urandom");
    
        boost::uintmax_t file_size = boost::filesystem::file_size(file);
        boost::uintmax_t filled = 0;
        char BUFFER[4096];
    
        while (file_size > filled)
        {
            if (::read(fdRandom, BUFFER, BUFFER_SIZE) != BUFFER_SIZE)
                throw std::system_error(errno, std::generic_category(), "Failed to read random data");
    
            if (::write(fd, BUFFER, BUFFER_SIZE) != BUFFER_SIZE)
                throw std::system_error(errno, std::generic_category(), "Failed to write random data");
                
            filled +=BUFFER_SIZE;
        }
    
        ::close(fd);
        ::close(fdRandom);
    }
    catch ( std::exception& ex )
    {
        if ( fd != -1 )
            ::close(fd);

        if ( fdRandom != -1 )
            ::close(fd);

        throw;
    }
}
