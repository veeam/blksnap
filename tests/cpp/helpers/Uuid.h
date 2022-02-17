/* [TBD]
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
