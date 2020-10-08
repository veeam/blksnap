project(blkSnapCppLib)

find_library(LIBUUID_LIBRARY libuuid.a REQUIRED)
if (NOT LIBUUID_LIBRARY)
    message(FATAL_ERROR "libuuid not found. please install uuid-dev package.")
endif ()

add_library(${PROJECT_NAME} STATIC
        src/Uuid.cpp
        src/Snapshot.cpp
        src/BlkSnapStoreCtx.cpp
        src/BlkSnapCtx.cpp
        src/Helper.cpp
        )

target_link_libraries(${PROJECT_NAME} PUBLIC
        BlkSnap::Lib
        )

target_link_libraries(${PROJECT_NAME} PRIVATE
        ${LIBUUID_LIBRARY}
        )

target_include_directories(${PROJECT_NAME} PUBLIC ./include)
add_library(BlkSnapCpp::Lib ALIAS ${PROJECT_NAME})
