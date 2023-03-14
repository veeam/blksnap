# SPDX-License-Identifier: GPL-2.0+

cmake_minimum_required(VERSION 3.5)
project(blksnap)

set(CMAKE_CXX_STANDARD 14)

add_subdirectory(${CMAKE_SOURCE_DIR}/lib/blksnap)
add_subdirectory(${CMAKE_SOURCE_DIR}/tools/blksnap)
add_subdirectory(${CMAKE_SOURCE_DIR}/tests/cpp)

if(EXISTS ${CMAKE_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in)
    configure_file(${CMAKE_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in
                ${CMAKE_SOURCE_DIR}/cmake/cmake_uninstall.cmake @ONLY
    )
    add_custom_target(uninstall "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/cmake/cmake_uninstall.cmake")
endif()