# [TBD]
# Copyright (C) 2022 Veeam Software Group GmbH <https://www.veeam.com/contacts.html>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.5)
project(blksnap)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -static-libgcc -pthread")

set(Boost_USE_STATIC_LIBS ON)
FIND_PACKAGE( Boost COMPONENTS filesystem REQUIRED)

find_library(LIBUUID_LIBRARY libuuid.a REQUIRED)
if (NOT LIBUUID_LIBRARY)
    message(FATAL_ERROR "libuuid not found. please install uuid-dev package.")
endif ()

include_directories(${CMAKE_SOURCE_DIR}/../../include)

set(SOURCE_FILES
    Blksnap.cpp
    Cbt.cpp
    Service.cpp
    Session.cpp
)

add_library(${PROJECT_NAME} ${SOURCE_FILES})

set_target_properties(${PROJECT_NAME}
    PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}../../
    #LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}../../
    #RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}../../
)
