cmake_minimum_required(VERSION 3.15)
project(voxel6d C)

set(CMAKE_C_STANDARD 11)

find_package(raylib QUIET)
if (NOT raylib_FOUND)
    include(FetchContent)
    FetchContent_Declare(raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.0)
    FetchContent_MakeAvailable(raylib)
endif()

add_executable(voxel6d main.c)
target_link_libraries(voxel6d raylib m)
