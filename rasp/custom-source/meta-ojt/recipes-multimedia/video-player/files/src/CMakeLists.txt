cmake_minimum_required(VERSION 3.10)
project(video-player VERSION 1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_PLAYER_TEST "Build separate UART/CLI test executable" ON)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)

# Player logic shared by the daemon and the CLI test
set(PLAYER_COMMON_SOURCES
    PlayerCore.cpp
    GStreamerBackend.cpp
)

# Daemon: PlayerCore + D-Bus
add_executable(video-player
    main.cpp
    PlayerDbus.cpp
    ${PLAYER_COMMON_SOURCES}
)
target_compile_options(video-player PRIVATE -Wall -Wextra)
target_link_libraries(video-player PRIVATE PkgConfig::GST PkgConfig::GIO Threads::Threads)
install(TARGETS video-player RUNTIME DESTINATION bin)

# CLI test (keyboard commands, no D-Bus)
if(BUILD_PLAYER_TEST)
    add_executable(player-test
        test/player_test.cpp
        ${PLAYER_COMMON_SOURCES}
    )
    target_include_directories(player-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(player-test PRIVATE PkgConfig::GST Threads::Threads)
    install(TARGETS player-test RUNTIME DESTINATION bin)
endif()
