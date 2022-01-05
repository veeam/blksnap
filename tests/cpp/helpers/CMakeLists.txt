cmake_minimum_required(VERSION 3.5)
project(helpers)

set(SOURCE_FILES
    Log.cpp
    BlockDevice.cpp
    RandomHelper.cpp
)
add_library(${PROJECT_NAME} ${SOURCE_FILES})
add_library(Helpers::Lib ALIAS ${PROJECT_NAME})
