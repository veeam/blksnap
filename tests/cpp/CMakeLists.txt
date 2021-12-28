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
set(CORRUPT_TEST corrupt)
add_executable(${CORRUPT_TEST} ${CORRUPT_SRC})
target_link_libraries(${CORRUPT_TEST} PUBLIC Blksnap::Lib)
target_link_libraries(${CORRUPT_TEST} PUBLIC Helpers::Lib)
target_link_libraries(${CORRUPT_TEST} PRIVATE Boost::program_options)
target_link_libraries(${CORRUPT_TEST} PRIVATE Boost::filesystem )
target_link_libraries(${CORRUPT_TEST} PRIVATE ${LIBUUID_LIBRARY})
target_link_libraries(${CORRUPT_TEST} PRIVATE ${LIBZ_LIBRARY})
target_include_directories(${CORRUPT_TEST} PRIVATE ./)

set(CBT_SRC
    cbt.cpp
)
set(CBT_TEST cbt)
add_executable(${CBT_TEST} ${CBT_SRC})
target_link_libraries(${CBT_TEST} PUBLIC Blksnap::Lib)
target_link_libraries(${CBT_TEST} PUBLIC Helpers::Lib)
target_link_libraries(${CBT_TEST} PRIVATE Boost::program_options)
target_link_libraries(${CBT_TEST} PRIVATE Boost::filesystem )
target_link_libraries(${CBT_TEST} PRIVATE ${LIBUUID_LIBRARY})
target_link_libraries(${CBT_TEST} PRIVATE ${LIBZ_LIBRARY})
target_include_directories(${CBT_TEST} PRIVATE ./)
