cmake_minimum_required(VERSION 3.21)
project(
    papo_tui
    VERSION 0.3.0
    LANGUAGES C)
include(GNUInstallDirs)
if(UNIX)
    set(CMAKE_INSTALL_RPATH
        "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}/papo-tui;$ORIGIN/../../${CMAKE_INSTALL_LIBDIR}/papo-tui"
    )
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
option(PAPO_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

option(PAPO_RELEASE_BUILD "Build an official version without a development suffix" OFF)
option(PAPO_COVERAGE "Instrument application and tests for gcovr" OFF)
if(PAPO_RELEASE_BUILD)
    set(PAPO_DISPLAY_VERSION "${PROJECT_VERSION}")
else()
    set(PAPO_DISPLAY_VERSION "${PROJECT_VERSION}-dev")
endif()
add_compile_definitions(PAPO_VERSION="${PAPO_DISPLAY_VERSION}")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(PAPO_COVERAGE)
    if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU" OR WIN32)
        message(FATAL_ERROR "Coverage requires GCC on Linux")
    endif()
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()
include(cmake/Dependencies.cmake)
include(cmake/Targets.cmake)
include(cmake/Tests.cmake)
include(cmake/Packaging.cmake)
