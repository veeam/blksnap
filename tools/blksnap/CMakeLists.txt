# SPDX-License-Identifier: GPL-2.0+

cmake_minimum_required(VERSION 3.5)
project(blksnap)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -static-libgcc")

set(Boost_USE_STATIC_LIBS ON)
FIND_PACKAGE( Boost COMPONENTS program_options filesystem REQUIRED)

find_library(LIBUUID_LIBRARY libuuid.a REQUIRED)
if (NOT LIBUUID_LIBRARY)
    message(FATAL_ERROR "libuuid not found. please install uuid-dev package.")
endif ()

include_directories(${CMAKE_SOURCE_DIR}/../../include)

set(SOURCE_FILES main.cpp)

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

target_link_libraries(${PROJECT_NAME} PRIVATE Boost::program_options)
target_link_libraries(${PROJECT_NAME} PRIVATE Boost::filesystem)
target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBUUID_LIBRARY})
target_include_directories(${PROJECT_NAME} PRIVATE ./)
