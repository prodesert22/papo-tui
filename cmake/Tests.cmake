include(CTest)
if(BUILD_TESTING)
    # Unit fixtures use assert for operations as well as postconditions. Release builds must
    # exercise those operations too.
    add_compile_options(-UNDEBUG)
endif()
if(BUILD_TESTING AND WIN32)
    include(${PROJECT_SOURCE_DIR}/cmake/WindowsTests.cmake)
    return()
endif()
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(permissions-probe tests/probes/permissions_probe.c)
    target_link_libraries(permissions-probe PRIVATE papo_outbox)
    target_compile_options(permissions-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(
        NAME permissions-integration
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_permissions.py
            $<TARGET_FILE:permissions-probe>)
    set_tests_properties(permissions-integration PROPERTIES TIMEOUT 120)
    add_executable(permissions-test tests/unit/test_permissions.c)
    target_link_libraries(permissions-test PRIVATE papo_auth)
    target_compile_options(permissions-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME permissions-unit COMMAND permissions-test)
    add_test(
        NAME permissions-terminal
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_permissions.py
            $<TARGET_FILE:papo-tui>)
    set_tests_properties(permissions-terminal PROPERTIES TIMEOUT 180)
    find_program(FFMPEG_CLI NAMES ffmpeg REQUIRED)
    add_test(
        NAME app-modes-terminal
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_app_modes.py
                $<TARGET_FILE:papo-tui> ${FFMPEG_CLI})
    set_tests_properties(app-modes-terminal PROPERTIES TIMEOUT 150)
    add_executable(media-wire-test tests/unit/test_media_wire.c)
    target_link_libraries(media-wire-test PRIVATE papo_media)
    target_compile_options(media-wire-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME media-wire-boundaries COMMAND media-wire-test)
    add_executable(media-service-test tests/integration/test_media_service.c)
    target_link_libraries(media-service-test PRIVATE papo_media)
    target_compile_options(media-service-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME media-service-integration COMMAND media-service-test)
    set_tests_properties(media-service-integration PROPERTIES TIMEOUT 45)
    add_executable(emoji-test tests/unit/test_emojis.c)
    target_link_libraries(emoji-test PRIVATE papo_media)
    target_compile_options(emoji-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME emojis-unit COMMAND emoji-test)
    add_executable(emoji-completion-test tests/unit/test_emoji_completion.c)
    target_link_libraries(emoji-completion-test PRIVATE papo_media)
    target_compile_options(emoji-completion-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME emoji-completion COMMAND emoji-completion-test)
    add_test(NAME emojis-catalogue
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/emoji_catalog.py
                     --check)
    add_test(
        NAME emojis-terminal
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_emojis_terminal.py
            $<TARGET_FILE:papo-tui> ${FFMPEG_CLI})
    set_tests_properties(emojis-terminal PROPERTIES TIMEOUT 180)
    add_executable(interactions-probe tests/probes/interactions_probe.c)
    target_link_libraries(interactions-probe PRIVATE papo_media)
    target_compile_options(interactions-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(
        NAME message-interactions-integration
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_message_interactions.py
            $<TARGET_FILE:interactions-probe>)
    set_tests_properties(message-interactions-integration PROPERTIES TIMEOUT 90)
    add_executable(r8-probe tests/probes/r8_probe.c)
    target_link_libraries(r8-probe PRIVATE papo_auth)
    target_compile_options(r8-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME r8-integration
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_r8.py
                     $<TARGET_FILE:r8-probe>)
    set_tests_properties(r8-integration PROPERTIES TIMEOUT 120)
    add_test(
        NAME r8-terminal
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_r8_terminal.py
            $<TARGET_FILE:papo-tui>)
    set_tests_properties(r8-terminal PROPERTIES TIMEOUT 120)
    add_executable(inbox-badges-test tests/unit/test_inbox_badges.c)
    target_link_libraries(inbox-badges-test PRIVATE papo_auth)
    target_compile_options(inbox-badges-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME inbox-badges-unit COMMAND inbox-badges-test)
    add_executable(read-state-probe tests/probes/read_state_probe.c)
    target_link_libraries(read-state-probe PRIVATE papo_auth)
    target_compile_options(read-state-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(
        NAME read-state-persistence
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_read_state.py
            $<TARGET_FILE:read-state-probe>)
    set_tests_properties(read-state-persistence PROPERTIES TIMEOUT 60)
    if(PAPO_DESKTOP_NOTIFICATIONS AND GIO_FOUND)
        add_executable(fake-notifications tests/support/fake_notifications.c)
        target_link_libraries(fake-notifications PRIVATE PkgConfig::GIO PkgConfig::JANSSON)
        target_compile_options(fake-notifications PRIVATE -Wall -Wextra -Wpedantic -Werror)
        find_program(DBUS_NOTIFICATIONS_RUN_SESSION NAMES dbus-run-session REQUIRED)
        add_test(
            NAME desktop-notifications
            COMMAND
                ${DBUS_NOTIFICATIONS_RUN_SESSION}
                --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/support/keyring-bus.conf --
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_desktop_notifications.py
                $<TARGET_FILE:papo-tui> $<TARGET_FILE:fake-notifications>)
        set_tests_properties(desktop-notifications PROPERTIES TIMEOUT 120)
    endif()
    add_executable(status-bar-test tests/unit/test_status_bar.c)
    target_link_libraries(status-bar-test PRIVATE papo_send_editor)
    target_compile_options(status-bar-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME status-bar-unit COMMAND status-bar-test)
    add_test(
        NAME footer-terminal
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_footer_terminal.py
            $<TARGET_FILE:papo-tui>)
    set_tests_properties(footer-terminal PROPERTIES TIMEOUT 120)
    add_test(
        NAME typing-terminal
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_typing_terminal.py
            $<TARGET_FILE:papo-tui>)
    set_tests_properties(typing-terminal PROPERTIES TIMEOUT 120)
    add_test(
        NAME input-controls-terminal
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_input_controls.py
            $<TARGET_FILE:papo-tui>)
    set_tests_properties(input-controls-terminal PROPERTIES TIMEOUT 120)
    add_executable(media-probe tests/probes/media_probe.c)
    target_link_libraries(media-probe PRIVATE papo_auth)
    target_compile_options(media-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_executable(mentions-test tests/unit/test_mentions.c)
    target_link_libraries(mentions-test PRIVATE papo_media)
    target_compile_options(mentions-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME mentions-unit COMMAND mentions-test)
    add_test(
        NAME mentions-terminal
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_mentions.py
                $<TARGET_FILE:papo-tui>)
    set_tests_properties(mentions-terminal PROPERTIES TIMEOUT 120)
    add_library(media-metrics SHARED tests/support/media_metrics.c)
    target_link_libraries(media-metrics PRIVATE PkgConfig::CHAFA PkgConfig::AVCODEC
                                                Threads::Threads ${CMAKE_DL_LIBS})
    target_compile_options(media-metrics PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_executable(media-cache-probe tests/probes/media_cache_probe.c)
    target_link_libraries(media-cache-probe PRIVATE papo_media)
    target_link_options(media-cache-probe PRIVATE -Wl,--wrap=malloc -Wl,--wrap=calloc
                        -Wl,--wrap=strdup -Wl,--wrap=malloc_trim)
    if(PAPO_SANITIZERS)
        target_compile_definitions(media-cache-probe PRIVATE PAPO_TEST_SANITIZERS)
    endif()
    target_compile_options(media-cache-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(
        NAME media-cache
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_media_cache.py
            $<TARGET_FILE:media-cache-probe> ${FFMPEG_CLI})
    set_tests_properties(media-cache PROPERTIES TIMEOUT 120)
    add_library(fake-media-backend MODULE tests/support/fake_media_backend.c)
    target_include_directories(fake-media-backend PRIVATE src)
    target_link_libraries(fake-media-backend PRIVATE PkgConfig::NCURSES PkgConfig::GMODULE)
    target_compile_options(fake-media-backend PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(
        NAME media-backends
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_media_backends.py
            $<TARGET_FILE:papo-tui> ${FFMPEG_CLI} $<TARGET_FILE:fake-media-backend>
            $<TARGET_FILE:papo_image_codec> $<TARGET_FILE:papo_video_player>)
    set_tests_properties(media-backends PROPERTIES TIMEOUT 90)
    add_executable(media-test tests/unit/test_media.c src/ui/media/sixel_encode.c)
    target_link_libraries(media-test PRIVATE papo_media PkgConfig::AVUTIL PkgConfig::SIXEL)
    target_compile_options(media-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME media-layout-sixel COMMAND media-test)
    add_executable(media-modal-probe tests/probes/media_modal_probe.c src/ui/components/theme.c
                                     src/ui/components/button.c)
    target_link_libraries(media-modal-probe PRIVATE papo_media)
    target_compile_options(media-modal-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(
        NAME media-modal-terminal
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_media_modal.py
            $<TARGET_FILE:media-modal-probe>)
    set_tests_properties(media-modal-terminal PROPERTIES TIMEOUT 30)
    add_executable(image-decode-test tests/unit/test_image_decode.c)
    target_link_libraries(image-decode-test PRIVATE papo_media)
    target_compile_options(image-decode-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(
        NAME image-decode
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_image_decode.py
            $<TARGET_FILE:image-decode-test> ${FFMPEG_CLI})
    add_executable(image-buffer-test tests/unit/test_image_buffer.c)
    target_include_directories(image-buffer-test PRIVATE src)
    target_link_libraries(image-buffer-test PRIVATE PkgConfig::GMODULE PkgConfig::NCURSES)
    target_compile_options(image-buffer-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(PAPO_SANITIZERS)
        target_compile_options(image-buffer-test PRIVATE -fsanitize=address,undefined
                                                         -fno-omit-frame-pointer)
        target_link_options(image-buffer-test PRIVATE -fsanitize=address,undefined)
    endif()
    add_dependencies(image-buffer-test papo_image_codec)
    add_test(
        NAME image-buffer-boundaries
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_image_buffer.py
            $<TARGET_FILE:image-buffer-test> $<TARGET_FILE:papo_image_codec> ${FFMPEG_CLI})
    add_test(
        NAME media-integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_media.py
                $<TARGET_FILE:media-probe>)
    add_test(
        NAME media-terminal
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_media_terminal.py
            $<TARGET_FILE:papo-tui> ${FFMPEG_CLI})
    set_tests_properties(media-integration PROPERTIES TIMEOUT 120)
    set_tests_properties(media-terminal PROPERTIES TIMEOUT 300)
    find_program(OPENSSL_CLI NAMES openssl REQUIRED)
    add_executable(realtime-probe tests/probes/realtime_probe.c)
    target_link_libraries(realtime-probe PRIVATE papo_auth)
    target_compile_options(realtime-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(
        NAME realtime-integration
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_realtime.py
            $<TARGET_FILE:realtime-probe>)
    set_tests_properties(realtime-integration PROPERTIES TIMEOUT 120)
    add_test(
        NAME realtime-terminal
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_realtime_terminal.py
            $<TARGET_FILE:papo-tui>)
    set_tests_properties(realtime-terminal PROPERTIES TIMEOUT 120)
    add_executable(ws-output-test tests/unit/test_ws_output.c)
    target_link_libraries(ws-output-test PRIVATE papo_auth)
    target_compile_options(ws-output-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME websocket-partial-output COMMAND ws-output-test)
    find_program(DBUS_RUN_SESSION NAMES dbus-run-session)
    find_program(GDBUS_CLI NAMES gdbus)
    if(PAPO_DESKTOP_SESSIONS
       AND GIO_FOUND
       AND DBUS_RUN_SESSION
       AND GDBUS_CLI)
        add_executable(fake-keyring tests/support/fake_keyring.c)
        target_link_libraries(fake-keyring PRIVATE PkgConfig::GIO PkgConfig::JANSSON)
        target_compile_options(fake-keyring PRIVATE -Wall -Wextra -Wpedantic -Werror)
        add_test(
            NAME persistent-sessions
            COMMAND
                ${DBUS_RUN_SESSION}
                --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/support/keyring-bus.conf --
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_persistent_sessions.py
                $<TARGET_FILE:papo-tui> $<TARGET_FILE:fake-keyring>)
        set_tests_properties(persistent-sessions PROPERTIES TIMEOUT 180)
        if(SECRET_FOUND)
            set_tests_properties(persistent-sessions PROPERTIES ENVIRONMENT "PAPO_TEST_SECRET=1")
        endif()
    endif()
    add_executable(history-probe tests/probes/history_probe.c)
    target_link_libraries(history-probe PRIVATE papo_auth)
    target_compile_options(history-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(
        NAME history-integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_history.py
                $<TARGET_FILE:history-probe>)
    add_test(
        NAME message-interactions-terminal
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_message_interactions.py
            $<TARGET_FILE:papo-tui>)
    set_tests_properties(message-interactions-terminal PROPERTIES TIMEOUT 180)
    add_test(
        NAME message-mutations-terminal
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_message_mutations.py
            $<TARGET_FILE:papo-tui>)
    set_tests_properties(message-mutations-terminal PROPERTIES TIMEOUT 180)
    add_test(
        NAME history-terminal
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_history_terminal.py
            $<TARGET_FILE:papo-tui>)
    set_tests_properties(history-integration history-terminal PROPERTIES TIMEOUT 120)
    add_test(
        NAME voice-conversation-terminal
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_voice_conversation.py
            $<TARGET_FILE:papo-tui>)
    set_tests_properties(voice-conversation-terminal PROPERTIES TIMEOUT 120)
    add_test(
        NAME message-send-product
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_send_product.py
            $<TARGET_FILE:papo-tui>)
    set_tests_properties(message-send-product PROPERTIES TIMEOUT 120)
    add_executable(send-terminal-probe tests/probes/send_terminal_probe.c src/ui/components/theme.c)
    target_link_libraries(send-terminal-probe PRIVATE papo_send_editor)
    target_compile_options(send-terminal-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(
        NAME message-send-terminal
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_send_terminal.py
            $<TARGET_FILE:send-terminal-probe>)
    set_tests_properties(message-send-terminal PROPERTIES TIMEOUT 120)
    add_executable(send-probe tests/probes/send_probe.c)
    target_link_libraries(send-probe PRIVATE papo_outbox)
    target_compile_options(send-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(
        NAME message-send-integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_send.py
                $<TARGET_FILE:send-probe>)
    set_tests_properties(message-send-integration PROPERTIES TIMEOUT 120)
    add_executable(session-probe tests/probes/session_probe.c)
    target_link_libraries(session-probe PRIVATE papo_auth)
    target_compile_options(session-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_executable(session-time-test tests/unit/test_session_time.c)
    target_link_libraries(session-time-test PRIVATE papo_auth)
    target_compile_options(session-time-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME session-expiry COMMAND session-time-test)
    add_test(
        NAME session-integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_session.py
                $<TARGET_FILE:session-probe>)
    add_test(
        NAME session-terminal
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_session_terminal.py
            $<TARGET_FILE:papo-tui>)
    set_tests_properties(session-integration session-terminal PROPERTIES TIMEOUT 120)
    add_executable(auth-probe tests/probes/auth_probe.c)
    target_link_libraries(auth-probe PRIVATE papo_auth)
    target_compile_options(auth-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_executable(profiles-probe tests/probes/profiles_probe.c)
    target_link_libraries(profiles-probe PRIVATE papo_auth)
    target_compile_options(profiles-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(
        NAME instance-profiles
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_profiles.py
            $<TARGET_FILE:profiles-probe>)
    add_test(
        NAME profiles-terminal
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_profiles_terminal.py
            $<TARGET_FILE:papo-tui>)
    set_tests_properties(instance-profiles profiles-terminal PROPERTIES TIMEOUT 120)
    add_executable(ws-probe tests/probes/ws_probe.c)
    target_link_libraries(ws-probe PRIVATE papo_auth)
    target_compile_options(ws-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_executable(rate-limit-test tests/unit/test_rate_limit.c)
    target_link_libraries(rate-limit-test PRIVATE papo_auth)
    target_compile_options(rate-limit-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME request-rate-limit COMMAND rate-limit-test)
    add_test(
        NAME auth-integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_auth.py
                $<TARGET_FILE:auth-probe>)
    add_test(
        NAME login-terminal
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_terminal.py
                $<TARGET_FILE:papo-tui>)
    add_executable(text-test tests/unit/test_text.c src/ui/input/text.c src/model/text/mentions.c)
    target_include_directories(text-test PRIVATE src)
    target_link_libraries(text-test PRIVATE PkgConfig::GLIB PkgConfig::NCURSES PkgConfig::UNIBREAK
                                            PkgConfig::JANSSON)
    target_compile_options(text-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(PAPO_SANITIZERS)
        target_compile_options(text-test PRIVATE -fsanitize=address,undefined
                                                 -fno-omit-frame-pointer)
        target_link_options(text-test PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME unicode-editor COMMAND text-test)
    add_test(NAME websocket-integration
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_ws.py
                     $<TARGET_FILE:ws-probe>)
    set_tests_properties(websocket-integration PROPERTIES TIMEOUT 120)
    add_test(NAME api-contract COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests/contract
                                       -p test_contract.py)
    set_tests_properties(api-contract PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                                                 TIMEOUT 30)
    set_tests_properties(auth-integration login-terminal PROPERTIES TIMEOUT 45)
endif()

if(BUILD_TESTING)
    add_executable(unicode-portability-test tests/unit/test_unicode_portability.c)
    target_link_libraries(unicode-portability-test PRIVATE papo_send_editor)
    add_test(NAME unicode-portability COMMAND unicode-portability-test)
    add_executable(runtime-test tests/unit/test_runtime.c)
    target_link_libraries(runtime-test PRIVATE papo_platform)
    add_test(NAME platform-runtime COMMAND runtime-test)
    add_test(
        NAME release-cli
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_release.py
                $<TARGET_FILE:papo-tui> ${PAPO_DISPLAY_VERSION})
endif()

if(BUILD_TESTING)
    if(WIN32)
        set(PAPO_PYTHON_SEPARATOR "\\;")
    else()
        set(PAPO_PYTHON_SEPARATOR ":")
    endif()
    set(PAPO_TEST_PYTHON_DIRS
        "${PROJECT_SOURCE_DIR}/tests/support" "${PROJECT_SOURCE_DIR}/tests/terminal"
        "${PROJECT_SOURCE_DIR}/tests/integration" "${PROJECT_SOURCE_DIR}")
    list(JOIN PAPO_TEST_PYTHON_DIRS "${PAPO_PYTHON_SEPARATOR}" PAPO_TEST_PYTHONPATH)
    get_property(
        PAPO_TEST_NAMES
        DIRECTORY
        PROPERTY TESTS)
    foreach(test IN LISTS PAPO_TEST_NAMES)
        set_property(
            TEST ${test}
            APPEND
            PROPERTY ENVIRONMENT "PYTHONPATH=${PAPO_TEST_PYTHONPATH}")
    endforeach()
endif()
