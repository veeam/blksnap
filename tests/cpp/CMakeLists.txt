cmake_minimum_required(VERSION 3.5)
project(blksnap-tests)


add_compile_definitions(_FILE_OFFSET_BITS, 64)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -static-libgcc")

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

set(CORRUPT_SRC
        helpers/Blksnap.cpp
        helpers/FileHelper.cpp
        helpers/LoopDevice.cpp
        helpers/RandomHelper.cpp
        helpers/SHA256Calc.cpp
        helpers/Uuid.cpp
        corrupt.cpp
        )

add_executable(corrupt ${CORRUPTION_SRC})

target_link_libraries(${PROJECT_NAME} PRIVATE Boost::program_options)
target_link_libraries(${PROJECT_NAME} PRIVATE Boost::filesystem )
target_link_libraries(${PROJECT_NAME} PRIVATE Boost::thread )

target_link_libraries(${PROJECT_NAME} PRIVATE OpenSSL::Crypto )
target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBUUID_LIBRARY})
target_include_directories(${PROJECT_NAME} PRIVATE ./)

