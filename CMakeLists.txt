cmake_minimum_required(VERSION 3.10)
project(PGGameServer C)

set(CMAKE_C_STANDARD 11)

# Look for Box2D in standard locations
find_library(BOX2D_LIBRARY NAMES box2d_3 box2d libbox2d)
find_path(BOX2D_INCLUDE_DIR box2d/box2d.h)

if(NOT BOX2D_LIBRARY OR NOT BOX2D_INCLUDE_DIR)
    message(FATAL_ERROR "Box2D 3.0 not found. Please build and install Box2D 3.0 from source:\n"
            "git clone https://github.com/erincatto/box2d.git\n"
            "cd box2d && mkdir build && cd build\n"
            "cmake .. -DBOX2D_BUILD_SHARED=ON -DBOX2D_BUILD_STATIC=OFF\n"
            "make && sudo make install")
endif()

# Find raylib
find_package(raylib QUIET)
if (NOT raylib_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        raylib
        URL https://github.com/raysan5/raylib/archive/refs/tags/4.5.0.tar.gz
    )
    FetchContent_MakeAvailable(raylib)
endif()

# Add executable
add_executable(box2d_hello main.c)

# Include and link Box2D and raylib
target_include_directories(box2d_hello PRIVATE ${BOX2D_INCLUDE_DIR})
target_link_libraries(box2d_hello ${BOX2D_LIBRARY} raylib m)
