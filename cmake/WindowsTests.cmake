find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(FFMPEG_CLI ffmpeg REQUIRED)
add_test(NAME terminal-windows-lifecycle
         COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/unit/test_terminal_windows.py)
foreach(
    spec IN
    ITEMS
        "media-wire-test|tests/unit/test_media_wire.c|papo_media|media-wire-boundaries"
        "media-service-test|tests/integration/test_media_service.c|papo_media|media-service-integration"
        "permissions-test|tests/unit/test_permissions.c|papo_auth|permissions-unit"
        "rate-limit-test|tests/unit/test_rate_limit.c|papo_auth|request-rate-limit"
        "session-time-test|tests/unit/test_session_time.c|papo_auth|session-expiry"
        "status-bar-test|tests/unit/test_status_bar.c|papo_send_editor|status-bar-unit"
        "unicode-portability-test|tests/unit/test_unicode_portability.c|papo_send_editor|unicode-portability"
        "runtime-test|tests/unit/test_runtime.c|papo_platform|platform-runtime"
        "emoji-completion-test|tests/unit/test_emoji_completion.c|papo_media|emoji-completion"
        "windows-storage-test|tests/unit/test_windows_storage.c|papo_sessions|windows-storage")
    string(REPLACE "|" ";" fields "${spec}")
    list(GET fields 0 target)
    list(GET fields 1 source)
    list(GET fields 2 library)
    list(GET fields 3 test)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE ${library})
    add_test(NAME ${test} COMMAND ${target})
endforeach()
foreach(probe IN ITEMS auth session send history)
    add_executable(${probe}-probe tests/probes/${probe}_probe.c)
    target_link_libraries(${probe}-probe PRIVATE papo_outbox)
    add_test(NAME ${probe}-integration
             COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/integration/test_${probe}.py
                     $<TARGET_FILE:${probe}-probe>)
    set_tests_properties(${probe}-integration PROPERTIES TIMEOUT 180)
endforeach()
add_test(NAME release-cli
         COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/integration/test_release.py
                 $<TARGET_FILE:papo-tui> ${PAPO_DISPLAY_VERSION})
add_test(NAME windows-terminal
         COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/terminal/test_windows.py
                 $<TARGET_FILE:papo-tui>)
set_tests_properties(windows-terminal PROPERTIES TIMEOUT 180)
add_executable(windows-input-probe tests/probes/windows_input_probe.c
                                   src/platform/windows/terminal.c)
target_link_libraries(windows-input-probe PRIVATE papo_send_editor)
target_compile_options(windows-input-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME windows-input
         COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/terminal/test_windows_input.py
                 $<TARGET_FILE:windows-input-probe>)
set_tests_properties(windows-input PROPERTIES TIMEOUT 30)

add_test(
    NAME message-interactions-terminal
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/terminal/test_message_interactions.py
            $<TARGET_FILE:papo-tui>)
set_tests_properties(message-interactions-terminal PROPERTIES TIMEOUT 180)
add_test(
    NAME message-discovery-terminal
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/terminal/test_discovery_terminal.py
            $<TARGET_FILE:papo-tui>)
set_tests_properties(message-discovery-terminal PROPERTIES TIMEOUT 180)
get_property(
    names
    DIRECTORY
    PROPERTY TESTS)
foreach(test IN LISTS names)
    set_property(
        TEST ${test}
        APPEND
        PROPERTY
            ENVIRONMENT
            "PYTHONPATH=${PROJECT_SOURCE_DIR}/tests/support\;${PROJECT_SOURCE_DIR}/tests/terminal\;${PROJECT_SOURCE_DIR}/tests/integration\;${PROJECT_SOURCE_DIR}"
    )
endforeach()

add_executable(permissions-probe tests/probes/permissions_probe.c)
target_link_libraries(permissions-probe PRIVATE papo_outbox)
add_test(NAME permissions-integration
         COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/integration/test_permissions.py
                 $<TARGET_FILE:permissions-probe>)
add_test(NAME permissions-terminal
         COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/terminal/test_permissions.py
                 $<TARGET_FILE:papo-tui>)
set_tests_properties(permissions-integration permissions-terminal PROPERTIES TIMEOUT 180)
