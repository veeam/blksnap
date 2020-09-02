cmake_minimum_required(VERSION 3.5)
project(BlkSnapLib)

set(SOURCE_FILES
    ./snapshot_ctl.c
    )

add_library(${PROJECT_NAME} ${SOURCE_FILES})
target_include_directories(${PROJECT_NAME} PUBLIC ../include)
