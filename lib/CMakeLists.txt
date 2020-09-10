cmake_minimum_required(VERSION 3.5)
project(BlkSnapLib)

set(SOURCE_FILES
    ./snapshot_ctl.c
    ./utils.c
    stretch_snapshot_impl.c
    stretch_snapshot.c
    )

add_library(${PROJECT_NAME} ${SOURCE_FILES})
target_include_directories(${PROJECT_NAME} PUBLIC ../include)
