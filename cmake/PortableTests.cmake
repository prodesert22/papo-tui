# Portable declarations are shared; OS adapters register their own additional scenarios.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(PAPO_VOICE)
    add_executable(voice-test tests/unit/test_voice.c)
    target_link_libraries(voice-test PRIVATE papo_auth)
    target_compile_options(voice-test PRIVATE -UNDEBUG -Wall -Wextra -Wpedantic -Werror)
    papo_add_test(NAME voice-state-unit COMMAND voice-test LABELS voice)
    add_executable(voice-audio-test tests/integration/test_voice_audio.c
                                    tests/support/voice_devices.c)
    target_include_directories(voice-audio-test PRIVATE tests/support)
    target_link_libraries(voice-audio-test PRIVATE papo_auth)
    target_compile_options(voice-audio-test PRIVATE -UNDEBUG -Wall -Wextra -Wpedantic -Werror)
    papo_add_test(
        NAME
        voice-audio-integration
        COMMAND
        voice-audio-test
        LABELS
        voice
        audio)
    papo_set_tests_properties(voice-audio-integration PROPERTIES TIMEOUT 25)
endif()
add_executable(channel-mutes-test tests/unit/test_channel_mutes.c)
target_link_libraries(channel-mutes-test PRIVATE papo_auth)
papo_add_test(NAME channel-mutes-unit COMMAND channel-mutes-test LABELS notifications)
add_executable(channel-notifications-test tests/unit/test_channel_notifications.c)
target_link_libraries(channel-notifications-test PRIVATE papo_auth)
papo_add_test(NAME channel-notifications-unit COMMAND channel-notifications-test LABELS
              notifications)
papo_add_test(
    NAME
    terminal-windows-lifecycle
    COMMAND
    ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tests/unit/test_terminal_windows.py
    LABELS
    platform
    harness)
add_executable(user-config-test tests/unit/test_user_config.c)
target_link_libraries(user-config-test PRIVATE papo_auth)
target_compile_options(user-config-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
papo_add_test(NAME user-config-unit COMMAND user-config-test LABELS settings)
add_executable(sound-test tests/unit/test_sound.c)
target_link_libraries(sound-test PRIVATE papo_send_editor)
target_compile_options(sound-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_dependencies(sound-test papo_sound_player)
papo_add_test(NAME sound-playback COMMAND sound-test LABELS audio)
add_executable(theme-background-test tests/unit/test_theme_background.c)
target_link_libraries(theme-background-test PRIVATE papo_send_editor)
target_compile_options(theme-background-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
papo_add_test(
    NAME
    theme-background-unit
    COMMAND
    theme-background-test
    LABELS
    settings
    ui)
papo_add_test(
    NAME
    sound-assets
    COMMAND
    ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tools/sound_assets.py
    --check
    LABELS
    audio)
add_executable(gif-search-test tests/unit/test_gif_search.c)
target_link_libraries(gif-search-test PRIVATE papo_auth)
papo_add_test(
    NAME
    gif-search-unit
    COMMAND
    gif-search-test
    LABELS
    media
    network)
add_executable(media-wire-test tests/unit/test_media_wire.c)
target_link_libraries(media-wire-test PRIVATE papo_media)
papo_add_test(NAME media-wire-boundaries COMMAND media-wire-test LABELS media)
add_executable(media-stream-test tests/unit/test_media_stream.c)
target_link_libraries(media-stream-test PRIVATE papo_media)
papo_add_test(NAME media-stream-boundaries COMMAND media-stream-test LABELS media)
papo_set_tests_properties(media-stream-boundaries PROPERTIES TIMEOUT 60)
add_executable(media-service-test tests/integration/test_media_service.c)
target_link_libraries(media-service-test PRIVATE papo_media)
papo_add_test(NAME media-service-integration COMMAND media-service-test LABELS media)
add_executable(permissions-test tests/unit/test_permissions.c)
target_link_libraries(permissions-test PRIVATE papo_auth)
papo_add_test(NAME permissions-unit COMMAND permissions-test LABELS permissions)
add_executable(rate-limit-test tests/unit/test_rate_limit.c)
target_link_libraries(rate-limit-test PRIVATE papo_auth)
papo_add_test(NAME request-rate-limit COMMAND rate-limit-test LABELS network)
add_executable(session-time-test tests/unit/test_session_time.c)
target_link_libraries(session-time-test PRIVATE papo_auth)
papo_add_test(NAME session-expiry COMMAND session-time-test LABELS auth)
add_executable(status-bar-test tests/unit/test_status_bar.c)
target_link_libraries(status-bar-test PRIVATE papo_send_editor)
papo_add_test(NAME status-bar-unit COMMAND status-bar-test LABELS ui)
add_executable(unicode-portability-test tests/unit/test_unicode_portability.c)
target_link_libraries(unicode-portability-test PRIVATE papo_send_editor)
papo_add_test(
    NAME
    unicode-portability
    COMMAND
    unicode-portability-test
    LABELS
    text
    platform)
add_executable(runtime-test tests/unit/test_runtime.c)
target_link_libraries(runtime-test PRIVATE papo_platform)
papo_add_test(NAME platform-runtime COMMAND runtime-test LABELS platform)
add_executable(emoji-completion-test tests/unit/test_emoji_completion.c)
target_link_libraries(emoji-completion-test PRIVATE papo_media)
papo_add_test(
    NAME
    emoji-completion
    COMMAND
    emoji-completion-test
    LABELS
    emoji
    text)
add_executable(auth-probe tests/probes/auth_probe.c)
target_link_libraries(auth-probe PRIVATE papo_auth)
papo_add_test(
    NAME
    auth-integration
    COMMAND
    ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tests/integration/test_auth.py
    $<TARGET_FILE:auth-probe>
    LABELS
    auth
    network)
add_executable(session-probe tests/probes/session_probe.c)
target_link_libraries(session-probe PRIVATE papo_auth)
papo_add_test(
    NAME
    session-integration
    COMMAND
    ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tests/integration/test_session.py
    $<TARGET_FILE:session-probe>
    LABELS
    auth
    network)
add_executable(send-probe tests/probes/send_probe.c)
target_link_libraries(send-probe PRIVATE papo_outbox)
papo_add_test(
    NAME
    message-send-integration
    COMMAND
    ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tests/integration/test_send.py
    $<TARGET_FILE:send-probe>
    LABELS
    send)
add_executable(history-probe tests/probes/history_probe.c)
target_link_libraries(history-probe PRIVATE papo_auth)
papo_add_test(
    NAME
    history-integration
    COMMAND
    ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tests/integration/test_history.py
    $<TARGET_FILE:history-probe>
    LABELS
    history)
add_executable(permissions-probe tests/probes/permissions_probe.c)
target_link_libraries(permissions-probe PRIVATE papo_outbox)
papo_add_test(
    NAME
    permissions-integration
    COMMAND
    ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tests/integration/test_permissions.py
    $<TARGET_FILE:permissions-probe>
    LABELS
    permissions)
papo_add_test(
    NAME
    release-cli
    COMMAND
    ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tests/integration/test_release.py
    $<TARGET_FILE:papo-tui>
    ${PAPO_DISPLAY_VERSION}
    LABELS
    packaging)
papo_add_test(
    NAME
    terminal-title
    COMMAND
    ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tests/terminal/test_terminal_title.py
    $<TARGET_FILE:papo-tui>
    LABELS
    ui
    notifications)
papo_add_test(
    NAME
    message-interactions-terminal
    COMMAND
    ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tests/terminal/test_message_interactions.py
    $<TARGET_FILE:papo-tui>
    LABELS
    interactions)
papo_add_test(
    NAME
    message-discovery-terminal
    COMMAND
    ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tests/terminal/test_discovery_terminal.py
    $<TARGET_FILE:papo-tui>
    LABELS
    search)
papo_add_test(
    NAME
    permissions-terminal
    COMMAND
    ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tests/terminal/test_permissions.py
    $<TARGET_FILE:papo-tui>
    LABELS
    permissions)
papo_add_test(
    NAME
    members-terminal
    COMMAND
    ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tests/terminal/test_members.py
    $<TARGET_FILE:papo-tui>
    LABELS
    members)
papo_add_test(
    NAME
    profile-edit-terminal
    COMMAND
    ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tests/terminal/test_profile_edit.py
    $<TARGET_FILE:papo-tui>
    LABELS
    profiles)
papo_add_test(
    NAME
    password-terminal
    COMMAND
    ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tests/terminal/test_password_terminal.py
    $<TARGET_FILE:papo-tui>
    LABELS
    auth)
papo_add_test(
    NAME
    user-settings-terminal
    COMMAND
    ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tests/terminal/test_user_settings_terminal.py
    $<TARGET_FILE:papo-tui>
    LABELS
    settings)
papo_add_test(
    NAME
    server-settings-terminal
    COMMAND
    ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tests/terminal/test_server_settings.py
    $<TARGET_FILE:papo-tui>
    LABELS
    admin)

papo_add_test(
    NAME
    terminal-stream-unit
    COMMAND
    ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tests/unit/test_terminal_stream.py
    LABELS
    harness)
