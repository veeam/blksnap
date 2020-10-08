project(blk-snap-tests)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -static-libgcc")

find_library(LIBUUID_LIBRARY libuuid.a REQUIRED)
find_package(OpenSSL REQUIRED)

if (NOT LIBUUID_LIBRARY)
    message(FATAL_ERROR "libuuid not found. please install uuid-dev package.")
endif ()

if (NOT OPENSSL_LIBRARIES)
    message(FATAL_ERROR "openssl not found. please install libssl-dev package.")
endif ()

set(SOURCE_FILES
        helpers/LoopDevice.cpp
        helpers/Uuid.cpp
        helpers/SHA256Calc.cpp
        helpers/FileHelper.cpp
        helpers/MountPoint.cpp
        helpers/SnapshotHelper.cpp
        LoopTests.cpp
        TestInMemorySnapshot.cpp
        CommonSnapStoreTest.cpp
        main.cpp
        )

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

target_link_libraries(${PROJECT_NAME} PRIVATE BlkSnap::Lib)
target_link_libraries(${PROJECT_NAME} PRIVATE Boost::program_options)
target_link_libraries(${PROJECT_NAME} PRIVATE Boost::filesystem )
target_link_libraries(${PROJECT_NAME} PRIVATE Boost::thread )
target_link_libraries(${PROJECT_NAME} PRIVATE BlkSnapCpp::Lib )

target_link_libraries(${PROJECT_NAME} PRIVATE ${OPENSSL_LIBRARIES} )
target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBUUID_LIBRARY})
target_include_directories(${PROJECT_NAME} PRIVATE ./)

