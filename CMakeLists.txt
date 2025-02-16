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
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/network
    ${CMAKE_SOURCE_DIR}/database
    ${CMAKE_SOURCE_DIR}/physics
    ${CMAKE_SOURCE_DIR}/world        # Make sure world is included
    ${CMAKE_SOURCE_DIR}/UI
    ${CMAKE_SOURCE_DIR}/.external  # Nuklear
    ${OPENSSL_INCLUDE_DIR}        # Changed from previous
    ${CURL_INCLUDE_DIRS}
)

# Optimization flags
set(CMAKE_C_FLAGS_RELEASE "-O3 -march=native -flto")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-O3")

# Common source files
set(COMMON_SOURCES
    .external/nuklear_raylib.c
    core/main.c
    physics/ship/ship_shapes.c
    UI/admin_console.c
    UI/admin_window.c
    world/coord_utils.c
    database/db_client.c
    network/websockets/websocket.c
    network/player_connection.c
    physics/player/player_physics.c
    env_loader.c
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
# target_compile_options(game_dashboard PRIVATE -O3 -march=native -flto)
# target_sources(game_dashboard PRIVATE
#     network/player_connection.c
# )

# Ensure proper Box2D linkage
target_link_libraries(game_dashboard PRIVATE
    box2d
    raylib
    m
    pthread
    ${CURL_LIBRARIES}
    ${OPENSSL_SSL_LIBRARIES}
    ${OPENSSL_CRYPTO_LIBRARIES}
)

# Add pthread library
find_package(Threads REQUIRED)
target_link_libraries(game_dashboard PRIVATE Threads::Threads)



