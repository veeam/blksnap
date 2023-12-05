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

add_subdirectory(helpers)

set(TESTS_LIBS blksnap-dev Helpers::Lib Boost::program_options Boost::filesystem ${LIBUUID_LIBRARY})

set(TEST_CORRUPT test_corrupt)
add_executable(${TEST_CORRUPT} TestSector.cpp corrupt.cpp)
target_link_libraries(${TEST_CORRUPT} PRIVATE ${TESTS_LIBS})
target_include_directories(${TEST_CORRUPT} PRIVATE ./)

set(TEST_CBT test_cbt)
add_executable(${TEST_CBT} cbt.cpp)
target_link_libraries(${TEST_CBT} PRIVATE ${TESTS_LIBS})
target_include_directories(${TEST_CBT} PRIVATE ./)

set(TEST_BOUNDARY test_boundary)
add_executable(${TEST_BOUNDARY} TestSector.cpp boundary.cpp)
target_link_libraries(${TEST_BOUNDARY} PRIVATE ${TESTS_LIBS})
target_include_directories(${TEST_BOUNDARY} PRIVATE ./)

set(TEST_PERFORMANCE test_performance)
add_executable(${TEST_PERFORMANCE} performance.cpp)
target_link_libraries(${TEST_PERFORMANCE} PRIVATE ${TESTS_LIBS})
target_include_directories(${TEST_PERFORMANCE} PRIVATE ./)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../
        DESTINATION /opt/blksnap/tests
        USE_SOURCE_PERMISSIONS
        PATTERN "*.sh"
        PATTERN "cpp" EXCLUDE
)

install(TARGETS ${TEST_CORRUPT} ${TEST_CBT} ${TEST_DIFF_STORAGE} ${TEST_BOUNDARY} ${TEST_PERFORMANCE}
        DESTINATION /opt/blksnap/tests
)
