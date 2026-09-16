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

include(CTest)

qt_add_executable(GitIdentityStudio WIN32
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/editcommitdialog.cpp
    src/editcommitdialog.h
    src/commitrewriter.cpp
    src/commitrewriter.h
    ui/mainwindow.ui
    ui/editcommitdialog.ui
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

if(BUILD_TESTING)
    qt_add_executable(CommitRewriterIntegrationTest
        tests/commitrewriter_integration.cpp
        src/commitrewriter.cpp
        src/commitrewriter.h
    )
    target_link_libraries(CommitRewriterIntegrationTest PRIVATE Qt6::Core)
    target_include_directories(CommitRewriterIntegrationTest PRIVATE src)

    add_test(NAME commit_rewriter_integration COMMAND CommitRewriterIntegrationTest)
    set_tests_properties(commit_rewriter_integration PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        TIMEOUT 120
    )
endif()
