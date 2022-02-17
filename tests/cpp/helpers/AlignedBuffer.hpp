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
#include <stdlib.h>

template<class T>
class AlignedBuffer
{
public:
    AlignedBuffer(size_t size)
        : m_alignment(size * sizeof(T))
        , m_size(size)
    {
        Allocate();
    };
    AlignedBuffer(size_t alignment, size_t size)
        : m_alignment(size)
        , m_size(size)
    {
        Allocate();
    };
    ~AlignedBuffer()
    {
        Free();
    };

    void Resize(size_t size)
    {
        Free();
        m_size = size;
        Allocate();
    };

    size_t Size()
    {
        return m_size;
    }

    T* Data()
    {
        return static_cast<T*>(m_buf);
    };

private:
    size_t m_alignment;
    size_t m_size;
    void* m_buf;

private:
    void Free()
    {
        if (m_buf)
        {
            ::free(m_buf);
            m_buf = nullptr;
        }
    };
    void Allocate()
    {
        m_buf = ::aligned_alloc(m_alignment, m_size * sizeof(T));
    };
};
