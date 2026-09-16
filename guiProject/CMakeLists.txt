cmake_minimum_required(VERSION 3.21)

project(GitIdentityStudio VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/ui")

find_package(Qt6 6.11 REQUIRED COMPONENTS Widgets)

qt_add_executable(GitIdentityStudio WIN32
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    ui/mainwindow.ui
    resources/resources.qrc
)

target_link_libraries(GitIdentityStudio PRIVATE Qt6::Widgets)

if(MSVC)
    target_compile_options(GitIdentityStudio PRIVATE /utf-8)
endif()

set_target_properties(GitIdentityStudio PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
