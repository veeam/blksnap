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
project(blksnap-tests)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -static-libgcc -pthread")

set(Boost_USE_STATIC_LIBS ON)
FIND_PACKAGE( Boost COMPONENTS program_options filesystem REQUIRED)

find_library(LIBUUID_LIBRARY libuuid.a REQUIRED)
if (NOT LIBUUID_LIBRARY)
    message(FATAL_ERROR "libuuid not found. please install uuid-dev package.")
endif ()

set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)
if (NOT OPENSSL_LIBRARIES)
    message(FATAL_ERROR "openssl not found. please install libssl-dev package.")
endif ()

include_directories(${CMAKE_SOURCE_DIR}/../../include)
set(BLKSNAP_LIBRARY "${CMAKE_SOURCE_DIR}/../../lib/libblksnap.a")
add_subdirectory(helpers)

set(CORRUPT_SRC
    corrupt.cpp
)
set(TEST_CORRUPT test_corrupt)
add_executable(${TEST_CORRUPT} ${CORRUPT_SRC})
target_link_libraries(${TEST_CORRUPT} PRIVATE Helpers::Lib)
target_link_libraries(${TEST_CORRUPT} PRIVATE ${BLKSNAP_LIBRARY})
target_link_libraries(${TEST_CORRUPT} PRIVATE Boost::program_options)
target_link_libraries(${TEST_CORRUPT} PRIVATE Boost::filesystem )
target_link_libraries(${TEST_CORRUPT} PRIVATE ${LIBUUID_LIBRARY})
target_include_directories(${TEST_CORRUPT} PRIVATE ./)
set_target_properties(${TEST_CORRUPT}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}../../
)

set(CBT_SRC
    cbt.cpp
)
set(TEST_CBT test_cbt)
add_executable(${TEST_CBT} ${CBT_SRC})
target_link_libraries(${TEST_CBT} PRIVATE Helpers::Lib)
target_link_libraries(${TEST_CBT} PRIVATE ${BLKSNAP_LIBRARY})
target_link_libraries(${TEST_CBT} PRIVATE Boost::program_options)
target_link_libraries(${TEST_CBT} PRIVATE Boost::filesystem )
target_link_libraries(${TEST_CBT} PRIVATE ${LIBUUID_LIBRARY})
target_link_libraries(${TEST_CBT} PRIVATE ${LIBZ_LIBRARY})
target_include_directories(${TEST_CBT} PRIVATE ./)
set_target_properties(${TEST_CBT}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}../../
)
