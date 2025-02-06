cmake_minimum_required(VERSION 3.10)
project(PGGameServer C)

set(CMAKE_C_STANDARD 11)

# Find Box2D
find_library(BOX2D_LIBRARY NAMES box2d_3 box2d libbox2d)
find_path(BOX2D_INCLUDE_DIR box2d/box2d.h)

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

# Add optimization flags
set(CMAKE_C_FLAGS_RELEASE "-O3 -march=native -flto")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-O3")

# Add Nuklear
include_directories(${CMAKE_SOURCE_DIR}/external)

# Create basic game dashboard executable
add_executable(game_dashboard 
    main.c
    ship_shapes.c
    admin_console.c
    admin_window.c
    coord_utils.c
    external/nuklear_raylib.c
)

target_include_directories(game_dashboard PRIVATE 
    ${BOX2D_INCLUDE_DIR}
)

target_link_libraries(game_dashboard PRIVATE
    ${BOX2D_LIBRARY}
    raylib
    m
    pthread
)

# Add optimization flags to release build
target_compile_options(game_dashboard PRIVATE -O3 -march=native -flto)

# Add debug build
add_executable(game_dashboard_debug 
    main.c
    ship_shapes.c
    admin_console.c
    admin_window.c
    coord_utils.c
    external/nuklear_raylib.c
)

target_include_directories(game_dashboard_debug PRIVATE 
    ${BOX2D_INCLUDE_DIR}
)

target_link_libraries(game_dashboard_debug PRIVATE
    ${BOX2D_LIBRARY}
    raylib
    m
    pthread
)

# Debug configuration
target_compile_options(game_dashboard_debug PRIVATE -g -O0)
