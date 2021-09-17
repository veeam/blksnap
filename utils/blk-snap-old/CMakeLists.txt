project(blk-snap)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -static-libgcc")

find_library(LIBUUID_LIBRARY libuuid.a REQUIRED)

if (NOT LIBUUID_LIBRARY)
    message(FATAL_ERROR "libuuid not found. please install uuid-dev package.")
endif ()

set(SOURCE_FILES
        main.cpp
        ArgsProcessor.cpp
        BlkSnapCtx.cpp
        BlkSnapStoreCtx.cpp
        Uuid.cpp
        Helper.cpp
        Snapshot.cpp
        commands/store/InMemoryStoreArgs.cpp
        commands/snapshot/CreateSnapshotArgs.cpp
        )

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

target_link_libraries(${PROJECT_NAME} PRIVATE BlkSnap::Lib)
target_link_libraries(${PROJECT_NAME} PRIVATE Boost::program_options)
target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBUUID_LIBRARY})
target_include_directories(${PROJECT_NAME} PRIVATE ./)