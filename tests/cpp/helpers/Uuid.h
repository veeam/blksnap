// SPDX-License-Identifier: GPL-2.0+
#pragma once

#include <string>
#include <uuid/uuid.h>

class Uuid
{
public:
    Uuid() noexcept;
    explicit Uuid(uuid_t uuid) noexcept;
    Uuid(const Uuid& other) noexcept;

    Uuid& operator=(const Uuid& other) noexcept;

    void Set(uuid_t other);
    void Clear();

    [[nodiscard]] std::string ToStr() const;

    bool operator==(const Uuid& other) const noexcept;
    bool operator!=(const Uuid& other) const noexcept;
    explicit operator bool() const noexcept;
    bool IsSet() const noexcept;

    const unsigned char* GetRaw() const;

    static Uuid GenerateRandom();
    static Uuid Parse(const std::string& str);
    static Uuid FromBuffer(const unsigned char* buf);

private:
    uuid_t m_uuid;
};

static inline std::ostream& operator<<(std::ostream& os, const Uuid& uuid)
{
    return os << uuid.ToStr();
}
