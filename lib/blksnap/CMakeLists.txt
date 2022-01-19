cmake_minimum_required(VERSION 3.5)
project(blksnap)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -static-libgcc -pthread")

set(Boost_USE_STATIC_LIBS ON)
FIND_PACKAGE( Boost COMPONENTS filesystem REQUIRED)

find_library(LIBUUID_LIBRARY libuuid.a REQUIRED)
if (NOT LIBUUID_LIBRARY)
    message(FATAL_ERROR "libuuid not found. please install uuid-dev package.")
endif ()

include_directories(${CMAKE_SOURCE_DIR}/../../include)

set(SOURCE_FILES
    Blksnap.cpp
    Cbt.cpp
    Service.cpp
    Session.cpp
)

add_library(${PROJECT_NAME} ${SOURCE_FILES})

set_target_properties(${PROJECT_NAME}
    PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}../../
    #LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}../../
    #RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}../../
)
