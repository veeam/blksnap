# [TBD]
# Copyright (C) 2022 Veeam Software Group GmbH <https://www.veeam.com/contacts.html>
#
# This file is part of blksnap-tests
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.5)
project(helpers)

set(SOURCE_FILES
    Log.cpp
    BlockDevice.cpp
    RandomHelper.cpp
)
add_library(${PROJECT_NAME} ${SOURCE_FILES})
add_library(Helpers::Lib ALIAS ${PROJECT_NAME})
