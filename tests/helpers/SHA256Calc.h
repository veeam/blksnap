#pragma once

#include <openssl/sha.h>

class SHA256Calc
{
public:
    SHA256Calc();
    ~SHA256Calc();
    
    void Final(unsigned char sha_hash[SHA256_DIGEST_LENGTH]);
    void Update(const void *data, size_t len);
    
private:
    SHA256_CTX m_sha_ctx;
};
