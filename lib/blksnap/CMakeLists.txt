#
# Copyright (C) 2022 Veeam Software Group GmbH <https://www.veeam.com/contacts.html>
#
# This file is part of libblksnap
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU Lesser General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option) any
# later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Lesser Public License for more
# details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.5)
project(blksnap-dev)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -static-libgcc -pthread")

set(Boost_USE_STATIC_LIBS ON)
FIND_PACKAGE( Boost COMPONENTS filesystem REQUIRED)

FIND_LIBRARY(LIBUUID_LIBRARY libuuid.so REQUIRED)
if (NOT LIBUUID_LIBRARY)
    message(FATAL_ERROR "libuuid not found. please install uuid-dev or libuuid-devel package.")
endif ()

set(SOURCE_FILES
    SnapshotCtl.cpp
    TrackerCtl.cpp
    Cbt.cpp
    Service.cpp
    Session.cpp
)

add_library(${PROJECT_NAME} STATIC ${SOURCE_FILES})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "blksnap")

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../include)

install(TARGETS ${PROJECT_NAME} DESTINATION /usr/lib)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../include/blksnap
        DESTINATION /usr/include/blksnap
        FILES_MATCHING PATTERN "*.h")
