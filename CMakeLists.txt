cmake_minimum_required(VERSION 3.21)
project(papo_tui VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
option(PAPO_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl>=7.86)
pkg_check_modules(JANSSON REQUIRED IMPORTED_TARGET jansson>=2.14)
pkg_check_modules(NCURSES REQUIRED IMPORTED_TARGET ncursesw>=6.1)
pkg_check_modules(FORM REQUIRED IMPORTED_TARGET formw>=6.1)
pkg_check_modules(UNIBREAK REQUIRED IMPORTED_TARGET libunibreak>=6.1)
option(PAPO_DESKTOP_SESSIONS "Enable optional desktop credential managers" ON)
if(PAPO_DESKTOP_SESSIONS)
    pkg_check_modules(GIO QUIET IMPORTED_TARGET gio-2.0>=2.66)
    pkg_check_modules(SECRET QUIET IMPORTED_TARGET libsecret-1>=0.20)
endif()

include(CheckCSourceRuns)
set(CMAKE_REQUIRED_LIBRARIES PkgConfig::CURL)
check_c_source_runs("#include <curl/curl.h>
#include <string.h>
int main(void) {
  const curl_version_info_data *v = curl_version_info(CURLVERSION_NOW);
  int ws = 0, wss = 0;
  if (!v || !v->protocols || !(v->features & CURL_VERSION_ASYNCHDNS) || !(v->features & CURL_VERSION_SSL)) return 1;
  for (const char *const *p = v->protocols; *p; ++p) {
    ws |= strcmp(*p, \"ws\") == 0; wss |= strcmp(*p, \"wss\") == 0;
  }
  return !(ws && wss);
}" PAPO_CURL_WS_SUPPORTED)
unset(CMAKE_REQUIRED_LIBRARIES)
if(NOT PAPO_CURL_WS_SUPPORTED)
    message(FATAL_ERROR "libcurl must provide WS/WSS, TLS and asynchronous DNS")
endif()

add_library(papo_auth src/net/auth.c src/net/session_time.c src/net/rate_limit.c src/net/ws.c src/config/profiles.c src/model/history.c)
target_include_directories(papo_auth PUBLIC src)
target_link_libraries(papo_auth PUBLIC PkgConfig::CURL PkgConfig::JANSSON)

add_library(papo_outbox src/app/outbox.c)
target_link_libraries(papo_outbox PUBLIC papo_auth PkgConfig::UNIBREAK)
target_compile_options(papo_outbox PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(papo_sessions src/app/session_store.c)
target_link_libraries(papo_sessions PUBLIC papo_auth)
target_compile_options(papo_sessions PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(PAPO_DESKTOP_SESSIONS AND GIO_FOUND)
    target_compile_definitions(papo_sessions PRIVATE HAVE_GIO)
    target_link_libraries(papo_sessions PRIVATE PkgConfig::GIO)
    if(SECRET_FOUND)
        target_compile_definitions(papo_sessions PRIVATE HAVE_LIBSECRET)
        target_link_libraries(papo_sessions PRIVATE PkgConfig::SECRET)
    endif()
endif()

add_library(papo_send_editor src/ui/send_editor.c src/ui/text.c src/app/instances.c)
target_link_libraries(papo_send_editor PUBLIC papo_outbox papo_sessions PkgConfig::NCURSES PkgConfig::UNIBREAK)
target_compile_options(papo_send_editor PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(papo-tui src/main.c src/ui/login.c src/ui/chat.c src/ui/history.c src/ui/profiles.c src/ui/theme.c src/ui/button.c)
target_link_libraries(papo-tui PRIVATE papo_send_editor PkgConfig::FORM PkgConfig::NCURSES PkgConfig::UNIBREAK)
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
    find_program(OPENSSL_CLI NAMES openssl REQUIRED)
    add_executable(realtime-probe tests/realtime_probe.c)
    target_link_libraries(realtime-probe PRIVATE papo_auth)
    target_compile_options(realtime-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME realtime-integration COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_realtime.py $<TARGET_FILE:realtime-probe>)
    set_tests_properties(realtime-integration PROPERTIES TIMEOUT 120)
    add_test(NAME realtime-terminal COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_realtime_terminal.py $<TARGET_FILE:papo-tui>)
    set_tests_properties(realtime-terminal PROPERTIES TIMEOUT 120)
    add_executable(ws-output-test tests/test_ws_output.c)
    target_link_libraries(ws-output-test PRIVATE papo_auth)
    target_compile_options(ws-output-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME websocket-partial-output COMMAND ws-output-test)
    find_program(DBUS_RUN_SESSION NAMES dbus-run-session)
    find_program(GDBUS_CLI NAMES gdbus)
    if(PAPO_DESKTOP_SESSIONS AND GIO_FOUND AND DBUS_RUN_SESSION AND GDBUS_CLI)
        add_executable(fake-keyring tests/fake_keyring.c)
        target_link_libraries(fake-keyring PRIVATE PkgConfig::GIO PkgConfig::JANSSON)
        target_compile_options(fake-keyring PRIVATE -Wall -Wextra -Wpedantic -Werror)
        add_test(NAME persistent-sessions COMMAND ${DBUS_RUN_SESSION}
            --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/keyring-bus.conf -- ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_persistent_sessions.py $<TARGET_FILE:papo-tui> $<TARGET_FILE:fake-keyring>)
        set_tests_properties(persistent-sessions PROPERTIES TIMEOUT 180)
        if(SECRET_FOUND)
            set_tests_properties(persistent-sessions PROPERTIES ENVIRONMENT "PAPO_TEST_SECRET=1")
        endif()
    endif()
    add_executable(history-probe tests/history_probe.c)
    target_link_libraries(history-probe PRIVATE papo_auth)
    target_compile_options(history-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME history-integration COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_history.py $<TARGET_FILE:history-probe>)
    add_test(NAME history-terminal COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_history_terminal.py $<TARGET_FILE:papo-tui>)
    set_tests_properties(history-integration history-terminal PROPERTIES TIMEOUT 120)
    add_test(NAME message-send-product COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_send_product.py $<TARGET_FILE:papo-tui>)
    set_tests_properties(message-send-product PROPERTIES TIMEOUT 120)
    add_executable(send-terminal-probe tests/send_terminal_probe.c src/ui/theme.c)
    target_link_libraries(send-terminal-probe PRIVATE papo_send_editor)
    target_compile_options(send-terminal-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME message-send-terminal COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_send_terminal.py $<TARGET_FILE:send-terminal-probe>)
    set_tests_properties(message-send-terminal PROPERTIES TIMEOUT 120)
    add_executable(send-probe tests/send_probe.c)
    target_link_libraries(send-probe PRIVATE papo_outbox)
    target_compile_options(send-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME message-send-integration COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_send.py $<TARGET_FILE:send-probe>)
    set_tests_properties(message-send-integration PROPERTIES TIMEOUT 120)
    add_executable(session-probe tests/session_probe.c)
    target_link_libraries(session-probe PRIVATE papo_auth)
    target_compile_options(session-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_executable(session-time-test tests/test_session_time.c)
    target_link_libraries(session-time-test PRIVATE papo_auth)
    target_compile_options(session-time-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME session-expiry COMMAND session-time-test)
    add_test(NAME session-integration COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_session.py $<TARGET_FILE:session-probe>)
    add_test(NAME session-terminal COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_session_terminal.py $<TARGET_FILE:papo-tui>)
    set_tests_properties(session-integration session-terminal PROPERTIES TIMEOUT 120)
    add_executable(auth-probe tests/auth_probe.c)
    target_link_libraries(auth-probe PRIVATE papo_auth)
    target_compile_options(auth-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_executable(profiles-probe tests/profiles_probe.c)
    target_link_libraries(profiles-probe PRIVATE papo_auth)
    target_compile_options(profiles-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME instance-profiles COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_profiles.py $<TARGET_FILE:profiles-probe>)
    add_test(NAME profiles-terminal COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_profiles_terminal.py $<TARGET_FILE:papo-tui>)
    set_tests_properties(instance-profiles profiles-terminal PROPERTIES TIMEOUT 120)
    add_executable(ws-probe tests/ws_probe.c)
    target_link_libraries(ws-probe PRIVATE papo_auth)
    target_compile_options(ws-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_executable(rate-limit-test tests/test_rate_limit.c)
    target_link_libraries(rate-limit-test PRIVATE papo_auth)
    target_compile_options(rate-limit-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME request-rate-limit COMMAND rate-limit-test)
    add_test(NAME auth-integration COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_auth.py $<TARGET_FILE:auth-probe>)
    add_test(NAME login-terminal COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_terminal.py $<TARGET_FILE:papo-tui>)
    add_executable(text-test tests/test_text.c src/ui/text.c)
    target_include_directories(text-test PRIVATE src)
    target_link_libraries(text-test PRIVATE PkgConfig::NCURSES PkgConfig::UNIBREAK)
    target_compile_options(text-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(PAPO_SANITIZERS)
        target_compile_options(text-test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(text-test PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME unicode-editor COMMAND text-test)
    add_test(NAME chat-terminal COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_chat.py $<TARGET_FILE:papo-tui>)
    set_tests_properties(chat-terminal PROPERTIES TIMEOUT 120)
    add_test(NAME websocket-integration COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ws.py $<TARGET_FILE:ws-probe>)
    set_tests_properties(websocket-integration PROPERTIES TIMEOUT 120)
    add_test(NAME api-contract COMMAND ${Python3_EXECUTABLE}
        -m unittest discover -s tests -p test_contract.py)
    set_tests_properties(api-contract PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 30)
    set_tests_properties(auth-integration login-terminal PROPERTIES TIMEOUT 45)
endif()

install(TARGETS papo-tui RUNTIME DESTINATION bin)
