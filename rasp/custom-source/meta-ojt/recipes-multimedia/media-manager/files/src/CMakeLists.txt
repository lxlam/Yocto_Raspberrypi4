cmake_minimum_required(VERSION 3.10)
project(ojt-media-manager C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET gio-2.0 libudev)

add_executable(ojt-media-manager
    media_manager_service.c
    usb_monitor.c
    playlist_scanner.c
)

target_compile_options(ojt-media-manager PRIVATE -Wall -Wextra)
target_link_libraries(ojt-media-manager PRIVATE PkgConfig::DEPS)

install(TARGETS ojt-media-manager DESTINATION bin)
