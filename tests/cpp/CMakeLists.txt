# SPDX-License-Identifier: GPL-2.0+

cmake_minimum_required(VERSION 3.5)
project(blksnap-tests)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -static-libgcc -pthread")

set(Boost_USE_STATIC_LIBS ON)
FIND_PACKAGE( Boost COMPONENTS program_options filesystem REQUIRED)

FIND_LIBRARY(LIBUUID_LIBRARY libuuid.so REQUIRED)
if (NOT LIBUUID_LIBRARY)
    message(FATAL_ERROR "libuuid not found. please install uuid-dev or libuuid-devel package.")
endif ()

set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)
if (NOT OPENSSL_LIBRARIES)
    message(FATAL_ERROR "openssl not found. please install libssl-dev package.")
endif ()

include_directories(${CMAKE_SOURCE_DIR}/../../include)
set(BLKSNAP_LIBRARY "${CMAKE_SOURCE_DIR}/../../lib/libblksnap.a")
add_subdirectory(helpers)

set(CORRUPT_SRC
    TestSector.cpp
    corrupt.cpp
)
set(TEST_CORRUPT test_corrupt)
add_executable(${TEST_CORRUPT} ${CORRUPT_SRC})
target_link_libraries(${TEST_CORRUPT} PRIVATE Helpers::Lib)
target_link_libraries(${TEST_CORRUPT} PRIVATE ${BLKSNAP_LIBRARY})
target_link_libraries(${TEST_CORRUPT} PRIVATE Boost::program_options)
target_link_libraries(${TEST_CORRUPT} PRIVATE Boost::filesystem )
target_link_libraries(${TEST_CORRUPT} PRIVATE ${LIBUUID_LIBRARY})
target_include_directories(${TEST_CORRUPT} PRIVATE ./)
set_target_properties(${TEST_CORRUPT}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}../../
)

set(CBT_SRC
    cbt.cpp
)
set(TEST_CBT test_cbt)
add_executable(${TEST_CBT} ${CBT_SRC})
target_link_libraries(${TEST_CBT} PRIVATE Helpers::Lib)
target_link_libraries(${TEST_CBT} PRIVATE ${BLKSNAP_LIBRARY})
target_link_libraries(${TEST_CBT} PRIVATE Boost::program_options)
target_link_libraries(${TEST_CBT} PRIVATE Boost::filesystem )
target_link_libraries(${TEST_CBT} PRIVATE ${LIBUUID_LIBRARY})
target_include_directories(${TEST_CBT} PRIVATE ./)
set_target_properties(${TEST_CBT}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}../../
)

set(DIFF_STORAGE_SRC
    TestSector.cpp
    diff_storage.cpp
)
set(TEST_DIFF_STORAGE test_diff_storage)
add_executable(${TEST_DIFF_STORAGE} ${DIFF_STORAGE_SRC})
target_link_libraries(${TEST_DIFF_STORAGE} PRIVATE Helpers::Lib)
target_link_libraries(${TEST_DIFF_STORAGE} PRIVATE ${BLKSNAP_LIBRARY})
target_link_libraries(${TEST_DIFF_STORAGE} PRIVATE Boost::program_options)
target_link_libraries(${TEST_DIFF_STORAGE} PRIVATE Boost::filesystem )
target_link_libraries(${TEST_DIFF_STORAGE} PRIVATE ${LIBUUID_LIBRARY})
target_include_directories(${TEST_DIFF_STORAGE} PRIVATE ./)
set_target_properties(${TEST_DIFF_STORAGE}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}../../
)

set(BOUNDARY_SRC
    TestSector.cpp
    boundary.cpp
)
set(TEST_BOUNDARY test_boundary)
add_executable(${TEST_BOUNDARY} ${BOUNDARY_SRC})
target_link_libraries(${TEST_BOUNDARY} PRIVATE Helpers::Lib)
target_link_libraries(${TEST_BOUNDARY} PRIVATE ${BLKSNAP_LIBRARY})
target_link_libraries(${TEST_BOUNDARY} PRIVATE Boost::program_options)
target_link_libraries(${TEST_BOUNDARY} PRIVATE Boost::filesystem )
target_link_libraries(${TEST_BOUNDARY} PRIVATE ${LIBUUID_LIBRARY})
target_include_directories(${TEST_BOUNDARY} PRIVATE ./)
set_target_properties(${TEST_BOUNDARY}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}../../
)

set(PERFORMANCE_SRC
    performance.cpp
)
set(TEST_PERFORMANCE test_performance)
add_executable(${TEST_PERFORMANCE} ${PERFORMANCE_SRC})
target_link_libraries(${TEST_PERFORMANCE} PRIVATE Helpers::Lib)
target_link_libraries(${TEST_PERFORMANCE} PRIVATE ${BLKSNAP_LIBRARY})
target_link_libraries(${TEST_PERFORMANCE} PRIVATE Boost::program_options)
target_link_libraries(${TEST_PERFORMANCE} PRIVATE Boost::filesystem )
target_link_libraries(${TEST_PERFORMANCE} PRIVATE ${LIBUUID_LIBRARY})
target_include_directories(${TEST_PERFORMANCE} PRIVATE ./)
set_target_properties(${TEST_PERFORMANCE}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}../../
)
