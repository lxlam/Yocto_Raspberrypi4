cmake_minimum_required(VERSION 3.10)
project(can-service VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0 gio-unix-2.0)

# Production binary: Linux SocketCAN (can0 = MCP2515 via mcp251x driver)
add_executable(can-service
    can_service.c
    dbus_adapter.c
    ojt_can_protocol.c
    can_link_socketcan.c
)
target_compile_options(can-service PRIVATE -Wall -Wextra)
target_link_libraries(can-service PRIVATE PkgConfig::GIO)

install(TARGETS can-service RUNTIME DESTINATION bin)

# Test programs live in ../test (separate CMake project, not built by Yocto)
