#include "../include/blk-snap-cpp/Uuid.h"

#include <iostream>
#include <sstream>

Uuid::Uuid() noexcept
{
    uuid_clear(m_uuid);
}

Uuid::Uuid(uuid_t uuid) noexcept
{
    uuid_copy(m_uuid, uuid);
}

Uuid::Uuid(const Uuid& other) noexcept
{
    uuid_copy(m_uuid, other.m_uuid);
}


Uuid& Uuid::operator=(const Uuid& other) noexcept
{
    uuid_copy(m_uuid, other.m_uuid);
    return *this;
}

std::string Uuid::ToStr() const
{
    char str[UUID_STR_LEN];
    uuid_unparse(m_uuid, str);
    return str;
}

void Uuid::Set(uuid_t other)
{
    uuid_copy(m_uuid, other);
}

bool Uuid::operator==(const Uuid& other) const noexcept
{
    return uuid_compare(m_uuid, other.m_uuid) == 0;
}

bool Uuid::operator!=(const Uuid& other) const noexcept
{
    return uuid_compare(m_uuid, other.m_uuid) != 0;
}

Uuid::operator bool() const noexcept
{
    return !uuid_is_null(m_uuid);
}

Uuid Uuid::GenerateRandom()
{
    uuid_t uuid;
    uuid_generate(uuid);
    return Uuid(uuid);
}

Uuid Uuid::Parse(const std::string& str)
{

    uuid_t uuid;
    if (uuid_parse(str.c_str(), uuid) != 0)
    {
        std::stringstream ss;
        ss << "Failed to convert {" << str << "} to uuid.";
        throw std::runtime_error(ss.str());
    }

    return Uuid(uuid);
}

Uuid Uuid::FromBuffer(const unsigned char* buf)
{
    Uuid uuid;
    uuid_copy(uuid.m_uuid, buf);
    return uuid;
}

void Uuid::Clear()
{
    uuid_clear(m_uuid);
}

bool Uuid::IsSet() const noexcept
{
    return (bool)*this;
}

const unsigned char* Uuid::GetRaw() const
{
    return m_uuid;
}
