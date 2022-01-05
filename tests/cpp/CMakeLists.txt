cmake_minimum_required(VERSION 3.5)
project(blksnap-tests)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -static-libgcc -pthread")

set(Boost_USE_STATIC_LIBS ON)
FIND_PACKAGE( Boost COMPONENTS program_options filesystem REQUIRED)

find_library(LIBUUID_LIBRARY libuuid.a REQUIRED)
if (NOT LIBUUID_LIBRARY)
    message(FATAL_ERROR "libuuid not found. please install uuid-dev package.")
endif ()

set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)
if (NOT OPENSSL_LIBRARIES)
    message(FATAL_ERROR "openssl not found. please install libssl-dev package.")
endif ()

find_library(LIBZ_LIBRARY libz.a REQUIRED)
if (NOT LIBZ_LIBRARY)
    message(FATAL_ERROR "libz not found. please install libz-dev package.")
endif ()

add_subdirectory(blksnap)
add_subdirectory(helpers)

set(CORRUPT_SRC
    corrupt.cpp
)
set(TEST_CORRUPT test_corrupt)
add_executable(${TEST_CORRUPT} ${CORRUPT_SRC})
target_link_libraries(${TEST_CORRUPT} PUBLIC Blksnap::Lib)
target_link_libraries(${TEST_CORRUPT} PUBLIC Helpers::Lib)
target_link_libraries(${TEST_CORRUPT} PRIVATE Boost::program_options)
target_link_libraries(${TEST_CORRUPT} PRIVATE Boost::filesystem )
target_link_libraries(${TEST_CORRUPT} PRIVATE ${LIBUUID_LIBRARY})
target_link_libraries(${TEST_CORRUPT} PRIVATE ${LIBZ_LIBRARY})
target_include_directories(${TEST_CORRUPT} PRIVATE ./)

set(CBT_SRC
    cbt.cpp
)
set(TEST_CBT test_cbt)
add_executable(${TEST_CBT} ${CBT_SRC})
target_link_libraries(${TEST_CBT} PUBLIC Blksnap::Lib)
target_link_libraries(${TEST_CBT} PUBLIC Helpers::Lib)
target_link_libraries(${TEST_CBT} PRIVATE Boost::program_options)
target_link_libraries(${TEST_CBT} PRIVATE Boost::filesystem )
target_link_libraries(${TEST_CBT} PRIVATE ${LIBUUID_LIBRARY})
target_link_libraries(${TEST_CBT} PRIVATE ${LIBZ_LIBRARY})
target_include_directories(${TEST_CBT} PRIVATE ./)
