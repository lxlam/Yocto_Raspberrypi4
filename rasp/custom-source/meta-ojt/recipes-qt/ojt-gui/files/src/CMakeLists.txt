cmake_minimum_required(VERSION 3.10)
project(ojt-gui VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)          # Qt: generate code for Q_OBJECT classes

find_package(Qt5 REQUIRED COMPONENTS Widgets DBus)

add_executable(ojt-gui
    main.cpp
    MainWindow.cpp
    MainWindow.h
    PlayerClient.cpp
    PlayerClient.h
)

target_compile_options(ojt-gui PRIVATE -Wall -Wextra)
target_link_libraries(ojt-gui PRIVATE Qt5::Widgets Qt5::DBus)

install(TARGETS ojt-gui RUNTIME DESTINATION bin)
