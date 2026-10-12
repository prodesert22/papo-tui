cmake_minimum_required(VERSION 3.21)
project(papo_tui VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
option(PAPO_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl>=7.85)
pkg_check_modules(JANSSON REQUIRED IMPORTED_TARGET jansson>=2.14)
pkg_check_modules(NCURSES REQUIRED IMPORTED_TARGET ncursesw>=6.1)
pkg_check_modules(FORM REQUIRED IMPORTED_TARGET formw>=6.1)

add_library(papo_auth src/net/auth.c)
target_include_directories(papo_auth PUBLIC src)
target_link_libraries(papo_auth PUBLIC PkgConfig::CURL PkgConfig::JANSSON)

add_executable(papo-tui src/main.c src/ui/login.c)
target_link_libraries(papo-tui PRIVATE papo_auth PkgConfig::FORM PkgConfig::NCURSES)
target_compile_definitions(papo-tui PRIVATE PAPO_VERSION="${PROJECT_VERSION}-dev")

foreach(target papo_auth papo-tui)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
endforeach()
if(PAPO_SANITIZERS)
    target_compile_options(papo_auth PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(papo_auth PUBLIC -fsanitize=address,undefined)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(auth-probe tests/auth_probe.c)
    target_link_libraries(auth-probe PRIVATE papo_auth)
    target_compile_options(auth-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME auth-integration COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_auth.py $<TARGET_FILE:auth-probe>)
    add_test(NAME login-terminal COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_terminal.py $<TARGET_FILE:papo-tui>)
    set_tests_properties(auth-integration login-terminal PROPERTIES TIMEOUT 45)
endif()

install(TARGETS papo-tui RUNTIME DESTINATION bin)
