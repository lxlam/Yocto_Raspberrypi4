# TEST ONLY - host build (PC). Not used by the Yocto recipe.
#   cmake -S test -B build-test && cmake --build build-test
cmake_minimum_required(VERSION 3.10)
project(can-service-tests LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0 gio-unix-2.0)

# 1. Protocol unit test (no D-Bus, no CAN)
add_executable(test_can_protocol test_can_protocol.c ${SRC}/ojt_can_protocol.c)
target_include_directories(test_can_protocol PRIVATE ${SRC})
target_compile_options(test_can_protocol PRIVATE -Wall -Wextra)

# 2. can-service with the simulated CAN link (same service code, other link file)
add_executable(can-service-sim
    ${SRC}/can_service.c
    ${SRC}/dbus_adapter.c
    ${SRC}/ojt_can_protocol.c
    can_link_sim.c
)
target_include_directories(can-service-sim PRIVATE ${SRC})
target_compile_options(can-service-sim PRIVATE -Wall -Wextra)
target_link_libraries(can-service-sim PRIVATE PkgConfig::GIO)
