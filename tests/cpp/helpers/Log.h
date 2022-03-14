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
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

class CLog
{
public:
    CLog()
        : m_isOpen(false){};
    ~CLog(){};

    void Open(const std::string& filename);

    void Info(const char* message);
    void Info(const std::string& message);
    void Info(const std::stringstream& ss);
    void Info(const void* buf, const size_t size);

    void Err(const char* message);
    void Err(const std::string& message);
    void Err(const std::stringstream& ss);
    void Err(const void* buf, const size_t size);

    void Detail(const char* message);
    void Detail(const std::string& message);
    void Detail(const std::stringstream& ss);
    void Detail(const void* buf, const size_t size);

private:
    std::mutex m_lock;
    bool m_isOpen;
    std::ofstream m_out;
};

extern CLog logger;
