cmake_minimum_required(VERSION 3.10)
project(PGGameServer C)

set(CMAKE_C_STANDARD 11)

# Core dependencies
find_package(OpenSSL REQUIRED)
if(NOT OPENSSL_FOUND)
    message(FATAL_ERROR "OpenSSL not found. Install with: sudo apt-get install libssl-dev")
endif()
find_package(CURL REQUIRED)
find_package(raylib QUIET)

# Find Box2D
find_library(BOX2D_LIBRARY NAMES box2d_3 box2d libbox2d)
find_path(BOX2D_INCLUDE_DIR box2d/box2d.h)

# Install raylib if not found
if (NOT raylib_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        raylib
        URL https://github.com/raysan5/raylib/archive/refs/tags/4.5.0.tar.gz
    )
    FetchContent_MakeAvailable(raylib)
endif()

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/external  # Nuklear
    ${OPENSSL_INCLUDE_DIR}        # Changed from previous
    ${CURL_INCLUDE_DIRS}
)

# Optimization flags
set(CMAKE_C_FLAGS_RELEASE "-O3 -march=native -flto")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-O3")

# Common source files
set(COMMON_SOURCES
    main.c
    ship_shapes.c
    admin_console.c
    admin_window.c
    coord_utils.c
    external/nuklear_raylib.c
    db_client.c
    net_protocol.c
    env_loader.c
    websocket.c
)

# Release build
add_executable(game_dashboard ${COMMON_SOURCES})
target_include_directories(game_dashboard PRIVATE ${BOX2D_INCLUDE_DIR})
target_link_libraries(game_dashboard PRIVATE
    ${BOX2D_LIBRARY}
    raylib
    m
    pthread
    ${CURL_LIBRARIES}
    ${OPENSSL_SSL_LIBRARIES}
    ${OPENSSL_CRYPTO_LIBRARIES}
)
target_compile_options(game_dashboard PRIVATE -O3 -march=native -flto)

# Debug build
add_executable(game_dashboard_debug ${COMMON_SOURCES})
target_include_directories(game_dashboard_debug PRIVATE ${BOX2D_INCLUDE_DIR})
target_link_libraries(game_dashboard_debug PRIVATE
    ${BOX2D_LIBRARY}
    raylib
    m
    pthread
    ${CURL_LIBRARIES}
    ${OPENSSL_SSL_LIBRARIES}
    ${OPENSSL_CRYPTO_LIBRARIES}
)
target_compile_options(game_dashboard_debug PRIVATE -g -O0)
