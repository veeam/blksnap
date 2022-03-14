/*
 * Copyright (C) 2022 Veeam Software Group GmbH <https://www.veeam.com/contacts.html>
 *
 * This file is part of blksnap-tests
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdexcept>

#include "SHA256Calc.h"

SHA256Calc::SHA256Calc()
{
    if (SHA256_Init(&m_sha_ctx) != 1)
        throw std::runtime_error("Failed to init SHA256_CTX");
}

SHA256Calc::~SHA256Calc()
{}

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
