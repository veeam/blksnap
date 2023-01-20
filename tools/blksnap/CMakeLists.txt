# SPDX-License-Identifier: GPL-2.0+

cmake_minimum_required(VERSION 3.5)
project(blksnap-tools)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -static-libgcc")

set(Boost_USE_STATIC_LIBS ON)
FIND_PACKAGE( Boost COMPONENTS program_options filesystem REQUIRED)

FIND_LIBRARY(LIBUUID_LIBRARY libuuid.so REQUIRED)
if (NOT LIBUUID_LIBRARY)
    message(FATAL_ERROR "libuuid not found. please install uuid-dev or libuuid-devel package.")
endif ()

add_executable(${PROJECT_NAME} main.cpp)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "blksnap")

target_compile_definitions(${PROJECT_NAME} PUBLIC BLK_SNAP_MODIFICATION)

include_directories(${CMAKE_SOURCE_DIR}/../../include)
set(TOOLS_LIBS ${LIBUUID_LIBRARY} Boost::filesystem Boost::program_options)
target_link_libraries(${PROJECT_NAME} PRIVATE ${TOOLS_LIBS})
target_include_directories(${PROJECT_NAME} PRIVATE ./)

install(TARGETS ${PROJECT_NAME} DESTINATION sbin)
