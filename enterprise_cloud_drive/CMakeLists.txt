cmake_minimum_required(VERSION 3.18)
project(EnterpriseCloudDrive LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BUILD_CLIENT "Build cloud drive client" ON)
option(BUILD_SERVER "Build cloud drive server" ON)

if(BUILD_SERVER)
    add_subdirectory(server)
endif()

if(BUILD_CLIENT)
    add_subdirectory(client)
endif()
