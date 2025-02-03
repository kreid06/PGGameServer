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

# Find CURL
find_package(CURL REQUIRED)

# Find JSON-C
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONC REQUIRED json-c)

# Find libwebsockets
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBWEBSOCKETS REQUIRED libwebsockets)

# Add executable with both main.c and network.c
add_executable(box2d_hello 
    main.c
    network.c
)

# Include and link Box2D, raylib, CURL, JSON-C, and libwebsockets
target_include_directories(box2d_hello PRIVATE 
    ${BOX2D_INCLUDE_DIR}
    ${RAYLIB_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${JSONC_INCLUDE_DIRS}
    ${LIBWEBSOCKETS_INCLUDE_DIRS}
)
target_link_libraries(box2d_hello PRIVATE
    ${BOX2D_LIBRARY}
    raylib
    ${CURL_LIBRARIES}
    ${JSONC_LIBRARIES}
    ${LIBWEBSOCKETS_LIBRARIES}
    m
)
