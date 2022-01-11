cmake_minimum_required(VERSION 3.5)
project(blksnap)

set(SOURCE_FILES
    Blksnap.cpp
    Cbt.cpp
    Service.spp
    Session.cpp
)
add_library(${PROJECT_NAME} ${SOURCE_FILES})
add_library(Blksnap::Lib ALIAS ${PROJECT_NAME})
