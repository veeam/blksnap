project(blk-snap)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -static-libgcc")

set(SOURCE_FILES
        main.cpp
        ArgsProcessor.cpp
        commands/store/InMemorySnapshotStore.cpp
        commands/store/InMemoryStoreArgs.cpp
        BlkSnapCtx.cpp
        )

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

target_link_libraries(${PROJECT_NAME} PRIVATE BlkSnap::Lib)
target_link_libraries(${PROJECT_NAME} PRIVATE Boost::program_options)
target_include_directories(${PROJECT_NAME} PRIVATE ./)