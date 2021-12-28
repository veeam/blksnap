cmake_minimum_required(VERSION 3.5)
project(libhelpers)

set(SOURCE_FILES
    BlockDevice.cpp
    RandomHelper.cpp
)
add_library(${PROJECT_NAME} ${SOURCE_FILES})
add_library(Helpers::Lib ALIAS ${PROJECT_NAME})
