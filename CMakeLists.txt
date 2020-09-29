cmake_minimum_required(VERSION 3.5)
project(blk-snap)

set(CMAKE_CXX_STANDARD 11)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bins/")

FIND_PACKAGE( Boost COMPONENTS program_options REQUIRED)

add_subdirectory(examples)
add_subdirectory(lib)
add_subdirectory(utils)
#add_subdirectory(module)

