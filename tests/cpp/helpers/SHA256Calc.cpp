#include "SHA256Calc.h"

#include <stdexcept>

SHA256Calc::SHA256Calc()
{
    if (SHA256_Init(&m_sha_ctx) != 1)
        throw std::runtime_error("Failed to init SHA256_CTX");
}

SHA256Calc::~SHA256Calc()
{
}

void SHA256Calc::Final(unsigned char* sha_hash)
{
    if (SHA256_Final(sha_hash, &m_sha_ctx) != 1)
        throw std::runtime_error("Failed to final SHA256_CTX");
}

void SHA256Calc::Update(const void* data, size_t len)
{
    if (SHA256_Update(&m_sha_ctx, data, len) != 1)
        throw std::runtime_error("Failed to update SHA256_CTX");
}
