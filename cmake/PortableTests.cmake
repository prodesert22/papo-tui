# Portable declarations are shared; OS adapters register their own additional scenarios.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
papo_add_test(NAME terminal-windows-lifecycle COMMAND ${Python3_EXECUTABLE}
              ${PROJECT_SOURCE_DIR}/tests/unit/test_terminal_windows.py)
add_executable(media-wire-test tests/unit/test_media_wire.c)
target_link_libraries(media-wire-test PRIVATE papo_media)
papo_add_test(NAME media-wire-boundaries COMMAND media-wire-test)
add_executable(media-service-test tests/integration/test_media_service.c)
target_link_libraries(media-service-test PRIVATE papo_media)
papo_add_test(NAME media-service-integration COMMAND media-service-test)
add_executable(permissions-test tests/unit/test_permissions.c)
target_link_libraries(permissions-test PRIVATE papo_auth)
papo_add_test(NAME permissions-unit COMMAND permissions-test)
add_executable(rate-limit-test tests/unit/test_rate_limit.c)
target_link_libraries(rate-limit-test PRIVATE papo_auth)
papo_add_test(NAME request-rate-limit COMMAND rate-limit-test)
add_executable(session-time-test tests/unit/test_session_time.c)
target_link_libraries(session-time-test PRIVATE papo_auth)
papo_add_test(NAME session-expiry COMMAND session-time-test)
add_executable(status-bar-test tests/unit/test_status_bar.c)
target_link_libraries(status-bar-test PRIVATE papo_send_editor)
papo_add_test(NAME status-bar-unit COMMAND status-bar-test)
add_executable(unicode-portability-test tests/unit/test_unicode_portability.c)
target_link_libraries(unicode-portability-test PRIVATE papo_send_editor)
papo_add_test(NAME unicode-portability COMMAND unicode-portability-test)
add_executable(runtime-test tests/unit/test_runtime.c)
target_link_libraries(runtime-test PRIVATE papo_platform)
papo_add_test(NAME platform-runtime COMMAND runtime-test)
add_executable(emoji-completion-test tests/unit/test_emoji_completion.c)
target_link_libraries(emoji-completion-test PRIVATE papo_media)
papo_add_test(NAME emoji-completion COMMAND emoji-completion-test)
add_executable(auth-probe tests/probes/auth_probe.c)
target_link_libraries(auth-probe PRIVATE papo_auth)
papo_add_test(NAME auth-integration COMMAND ${Python3_EXECUTABLE}
              ${PROJECT_SOURCE_DIR}/tests/integration/test_auth.py $<TARGET_FILE:auth-probe>)
add_executable(session-probe tests/probes/session_probe.c)
target_link_libraries(session-probe PRIVATE papo_auth)
papo_add_test(NAME session-integration COMMAND ${Python3_EXECUTABLE}
              ${PROJECT_SOURCE_DIR}/tests/integration/test_session.py $<TARGET_FILE:session-probe>)
add_executable(send-probe tests/probes/send_probe.c)
target_link_libraries(send-probe PRIVATE papo_outbox)
papo_add_test(NAME message-send-integration COMMAND ${Python3_EXECUTABLE}
              ${PROJECT_SOURCE_DIR}/tests/integration/test_send.py $<TARGET_FILE:send-probe>)
add_executable(history-probe tests/probes/history_probe.c)
target_link_libraries(history-probe PRIVATE papo_auth)
papo_add_test(NAME history-integration COMMAND ${Python3_EXECUTABLE}
              ${PROJECT_SOURCE_DIR}/tests/integration/test_history.py $<TARGET_FILE:history-probe>)
add_executable(permissions-probe tests/probes/permissions_probe.c)
target_link_libraries(permissions-probe PRIVATE papo_outbox)
papo_add_test(
    NAME permissions-integration COMMAND ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tests/integration/test_permissions.py $<TARGET_FILE:permissions-probe>)
papo_add_test(
    NAME
    release-cli
    COMMAND
    ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tests/integration/test_release.py
    $<TARGET_FILE:papo-tui>
    ${PAPO_DISPLAY_VERSION})
papo_add_test(NAME terminal-title COMMAND ${Python3_EXECUTABLE}
              ${PROJECT_SOURCE_DIR}/tests/terminal/test_terminal_title.py $<TARGET_FILE:papo-tui>)
papo_add_test(
    NAME message-interactions-terminal COMMAND ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tests/terminal/test_message_interactions.py $<TARGET_FILE:papo-tui>)
papo_add_test(
    NAME message-discovery-terminal COMMAND ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tests/terminal/test_discovery_terminal.py $<TARGET_FILE:papo-tui>)
papo_add_test(NAME permissions-terminal COMMAND ${Python3_EXECUTABLE}
              ${PROJECT_SOURCE_DIR}/tests/terminal/test_permissions.py $<TARGET_FILE:papo-tui>)
papo_add_test(NAME members-terminal COMMAND ${Python3_EXECUTABLE}
              ${PROJECT_SOURCE_DIR}/tests/terminal/test_members.py $<TARGET_FILE:papo-tui>)
