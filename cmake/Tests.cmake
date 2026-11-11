include(CTest)
if(BUILD_TESTING)
    include(${PROJECT_SOURCE_DIR}/cmake/TestRegistry.cmake)
    include(${PROJECT_SOURCE_DIR}/cmake/PortableTests.cmake)
endif()
if(BUILD_TESTING AND WIN32)
    include(${PROJECT_SOURCE_DIR}/cmake/WindowsTests.cmake)
    papo_finalize_tests()
    return()
endif()
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    papo_set_tests_properties(permissions-integration PROPERTIES TIMEOUT 120)

    papo_set_tests_properties(permissions-terminal PROPERTIES TIMEOUT 180)
    papo_add_test(
        NAME
        member-colors-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_member_colors.py
        $<TARGET_FILE:papo-tui>
        LABELS
        members)
    papo_set_tests_properties(member-colors-terminal PROPERTIES TIMEOUT 90)

    papo_set_tests_properties(members-terminal PROPERTIES TIMEOUT 180)
    find_program(FFMPEG_CLI NAMES ffmpeg REQUIRED)
    papo_add_test(
        NAME
        app-modes-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_app_modes.py
        $<TARGET_FILE:papo-tui>
        ${FFMPEG_CLI}
        LABELS
        settings
        media)
    papo_set_tests_properties(app-modes-terminal PROPERTIES TIMEOUT 150)
    papo_add_test(
        NAME
        members-media-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/terminal/test_members_media.py
        $<TARGET_FILE:papo-tui>
        ${FFMPEG_CLI}
        LABELS
        members
        media)
    papo_set_tests_properties(members-media-terminal PROPERTIES TIMEOUT 120)

    papo_set_tests_properties(media-service-integration PROPERTIES TIMEOUT 45)
    add_executable(emoji-test tests/unit/test_emojis.c)
    target_link_libraries(emoji-test PRIVATE papo_media)
    target_compile_options(emoji-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    papo_add_test(NAME emojis-unit COMMAND emoji-test LABELS emoji)

    papo_add_test(
        NAME
        emojis-catalogue
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/emoji_catalog.py
        --check
        LABELS
        emoji)
    papo_add_test(
        NAME
        emojis-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_emojis_terminal.py
        $<TARGET_FILE:papo-tui>
        ${FFMPEG_CLI}
        LABELS
        emoji)
    papo_set_tests_properties(emojis-terminal PROPERTIES TIMEOUT 210)
    add_executable(interactions-probe tests/probes/interactions_probe.c)
    target_link_libraries(interactions-probe PRIVATE papo_media)
    target_compile_options(interactions-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    papo_add_test(
        NAME
        message-interactions-integration
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_message_interactions.py
        $<TARGET_FILE:interactions-probe>
        LABELS
        interactions)
    papo_set_tests_properties(message-interactions-integration PROPERTIES TIMEOUT 90)
    add_executable(r8-probe tests/probes/r8_probe.c)
    target_link_libraries(r8-probe PRIVATE papo_auth)
    target_compile_options(r8-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    papo_add_test(
        NAME
        r8-integration
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_r8.py
        $<TARGET_FILE:r8-probe>
        LABELS
        dms
        notifications)
    papo_set_tests_properties(r8-integration PROPERTIES TIMEOUT 120)
    papo_add_test(
        NAME
        message-discovery-integration
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_discovery.py
        $<TARGET_FILE:r8-probe>
        LABELS
        search)
    papo_set_tests_properties(message-discovery-integration PROPERTIES TIMEOUT 180)
    papo_add_test(
        NAME
        r8-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_r8_terminal.py
        $<TARGET_FILE:papo-tui>
        LABELS
        dms
        notifications)
    papo_set_tests_properties(r8-terminal PROPERTIES TIMEOUT 120)

    papo_set_tests_properties(message-discovery-terminal PROPERTIES TIMEOUT 180)
    add_executable(inbox-badges-test tests/unit/test_inbox_badges.c)
    target_link_libraries(inbox-badges-test PRIVATE papo_auth)
    target_compile_options(inbox-badges-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    papo_add_test(NAME inbox-badges-unit COMMAND inbox-badges-test LABELS notifications)
    add_executable(reply-authors-test tests/unit/test_reply_authors.c)
    target_link_libraries(reply-authors-test PRIVATE papo_auth)
    target_compile_options(reply-authors-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    papo_add_test(NAME reply-authors-unit COMMAND reply-authors-test LABELS history)
    add_executable(search-recent-test tests/unit/test_search_recent.c)
    target_link_libraries(search-recent-test PRIVATE papo_auth)
    target_compile_options(search-recent-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    papo_add_test(NAME search-recent-unit COMMAND search-recent-test LABELS search)
    add_executable(user-directory-test tests/unit/test_user_directory.c)
    target_link_libraries(user-directory-test PRIVATE papo_auth)
    target_compile_options(user-directory-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    papo_add_test(NAME user-directory-unit COMMAND user-directory-test LABELS members)
    add_executable(draft-state-test tests/unit/test_draft_state.c)
    target_link_libraries(draft-state-test PRIVATE papo_auth)
    target_compile_options(draft-state-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    papo_add_test(NAME draft-state-unit COMMAND draft-state-test LABELS send)
    add_executable(server-admin-test tests/unit/test_server_admin.c)
    target_link_libraries(server-admin-test PRIVATE papo_auth)
    target_compile_options(server-admin-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    papo_add_test(NAME server-admin-unit COMMAND server-admin-test LABELS admin)
    add_executable(read-state-probe tests/probes/read_state_probe.c)
    target_link_libraries(read-state-probe PRIVATE papo_auth)
    target_compile_options(read-state-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    papo_add_test(
        NAME
        read-state-persistence
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_read_state.py
        $<TARGET_FILE:read-state-probe>
        LABELS
        notifications)
    papo_set_tests_properties(read-state-persistence PROPERTIES TIMEOUT 60)
    if(PAPO_DESKTOP_NOTIFICATIONS AND GIO_FOUND)
        add_executable(fake-notifications tests/support/fake_notifications.c)
        target_link_libraries(fake-notifications PRIVATE PkgConfig::GIO PkgConfig::JANSSON)
        target_compile_options(fake-notifications PRIVATE -Wall -Wextra -Wpedantic -Werror)
        find_program(DBUS_NOTIFICATIONS_RUN_SESSION NAMES dbus-run-session REQUIRED)
        papo_add_test(
            NAME
            desktop-notifications
            COMMAND
            ${DBUS_NOTIFICATIONS_RUN_SESSION}
            --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/support/keyring-bus.conf
            --
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_desktop_notifications.py
            $<TARGET_FILE:papo-tui>
            $<TARGET_FILE:fake-notifications>
            LABELS
            notifications
            platform)
        papo_set_tests_properties(desktop-notifications PROPERTIES TIMEOUT 120)
    endif()

    papo_set_tests_properties(terminal-title PROPERTIES TIMEOUT 90)
    papo_add_test(
        NAME
        footer-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_footer_terminal.py
        $<TARGET_FILE:papo-tui>
        LABELS
        ui)
    papo_set_tests_properties(footer-terminal PROPERTIES TIMEOUT 120)
    papo_add_test(
        NAME
        language-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_language_terminal.py
        $<TARGET_FILE:papo-tui>
        LABELS
        language
        ui)
    papo_add_test(
        NAME
        quick-switcher-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_quick_switcher.py
        $<TARGET_FILE:papo-tui>
        LABELS
        ui
        servers)
    papo_add_test(
        NAME
        typing-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_typing_terminal.py
        $<TARGET_FILE:papo-tui>
        LABELS
        realtime)
    papo_set_tests_properties(typing-terminal PROPERTIES TIMEOUT 120)
    papo_add_test(
        NAME
        input-controls-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_input_controls.py
        $<TARGET_FILE:papo-tui>
        LABELS
        input
        text)
    papo_set_tests_properties(input-controls-terminal PROPERTIES TIMEOUT 120)
    # Termux is Android-only and its terminal reports mouse taps through a POSIX pseudo terminal, so
    # the scenario stays outside the Windows suite.
    papo_add_test(
        NAME
        termux-keyboard-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_termux_keyboard.py
        $<TARGET_FILE:papo-tui>
        LABELS
        input
        platform)
    papo_set_tests_properties(termux-keyboard-terminal PROPERTIES TIMEOUT 120)
    add_executable(media-probe tests/probes/media_probe.c)
    target_link_libraries(media-probe PRIVATE papo_auth)
    target_compile_options(media-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_executable(mentions-test tests/unit/test_mentions.c)
    target_link_libraries(mentions-test PRIVATE papo_media)
    target_compile_options(mentions-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    papo_add_test(NAME mentions-unit COMMAND mentions-test LABELS text)
    papo_add_test(
        NAME
        mentions-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_mentions.py
        $<TARGET_FILE:papo-tui>
        LABELS
        text)
    papo_set_tests_properties(mentions-terminal PROPERTIES TIMEOUT 120)
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
    papo_add_test(
        NAME
        media-cache
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_media_cache.py
        $<TARGET_FILE:media-cache-probe>
        ${FFMPEG_CLI}
        LABELS
        media)
    papo_set_tests_properties(media-cache PROPERTIES TIMEOUT 120)
    add_library(fake-media-backend MODULE tests/support/fake_media_backend.c)
    target_include_directories(fake-media-backend PRIVATE src)
    target_link_libraries(fake-media-backend PRIVATE PkgConfig::NCURSES PkgConfig::GMODULE)
    target_compile_options(fake-media-backend PRIVATE -Wall -Wextra -Wpedantic -Werror)
    papo_add_test(
        NAME
        media-backends
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_media_backends.py
        $<TARGET_FILE:papo-tui>
        ${FFMPEG_CLI}
        $<TARGET_FILE:fake-media-backend>
        $<TARGET_FILE:papo_image_codec>
        $<TARGET_FILE:papo_video_player>
        LABELS
        media)
    papo_set_tests_properties(media-backends PROPERTIES TIMEOUT 90)
    add_executable(media-test tests/unit/test_media.c src/ui/media/sixel_encode.c)
    target_link_libraries(media-test PRIVATE papo_media PkgConfig::AVUTIL PkgConfig::SIXEL)
    target_compile_options(media-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    papo_add_test(NAME media-layout-sixel COMMAND media-test LABELS media)
    add_executable(profile-edit-test tests/unit/test_profile_edit.c)
    target_include_directories(profile-edit-test PRIVATE src)
    target_link_libraries(profile-edit-test PRIVATE papo_media PkgConfig::ZLIB)
    target_compile_options(profile-edit-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    papo_add_test(NAME profile-edit-unit COMMAND profile-edit-test LABELS profiles)
    add_executable(media-modal-probe tests/probes/media_modal_probe.c src/ui/components/button.c)
    target_link_libraries(media-modal-probe PRIVATE papo_media)
    target_compile_options(media-modal-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    papo_add_test(
        NAME
        media-modal-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_media_modal.py
        $<TARGET_FILE:media-modal-probe>
        LABELS
        media)
    papo_set_tests_properties(media-modal-terminal PROPERTIES TIMEOUT 30)
    add_executable(image-decode-test tests/unit/test_image_decode.c)
    target_link_libraries(image-decode-test PRIVATE papo_media)
    target_compile_options(image-decode-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    papo_add_test(
        NAME
        image-decode
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_image_decode.py
        $<TARGET_FILE:image-decode-test>
        ${FFMPEG_CLI}
        LABELS
        media)
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
    papo_add_test(
        NAME
        image-buffer-boundaries
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_image_buffer.py
        $<TARGET_FILE:image-buffer-test>
        $<TARGET_FILE:papo_image_codec>
        ${FFMPEG_CLI}
        LABELS
        media)
    papo_add_test(
        NAME
        media-integration
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_media.py
        $<TARGET_FILE:media-probe>
        LABELS
        media)
    papo_add_test(
        NAME
        media-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_media_terminal.py
        $<TARGET_FILE:papo-tui>
        ${FFMPEG_CLI}
        LABELS
        media)
    papo_set_tests_properties(media-integration PROPERTIES TIMEOUT 120)
    papo_add_test(
        NAME
        navigation-help-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/terminal/test_navigation_help.py
        $<TARGET_FILE:papo-tui>
        LABELS
        ui
        input)
    papo_set_tests_properties(media-terminal PROPERTIES TIMEOUT 420)
    papo_add_test(
        NAME
        channel-preferences-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/terminal/test_channel_preferences.py
        $<TARGET_FILE:papo-tui>
        ${FFMPEG_CLI}
        LABELS
        notifications)
    papo_add_test(
        NAME
        media-cadence-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_media_cadence.py
        $<TARGET_FILE:papo-tui>
        ${FFMPEG_CLI}
        LABELS
        media)
    papo_add_test(
        NAME
        media-navigation-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_media_navigation.py
        $<TARGET_FILE:papo-tui>
        ${FFMPEG_CLI}
        LABELS
        media)
    papo_set_tests_properties(media-navigation-terminal PROPERTIES TIMEOUT 180)
    papo_add_test(
        NAME
        gif-attachments-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_gif_attachments.py
        $<TARGET_FILE:papo-tui>
        ${FFMPEG_CLI}
        LABELS
        media
        send)
    papo_set_tests_properties(gif-attachments-terminal PROPERTIES TIMEOUT 180)
    papo_add_test(
        NAME
        gif-picker-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_gif_picker.py
        $<TARGET_FILE:papo-tui>
        ${FFMPEG_CLI}
        LABELS
        media)
    papo_set_tests_properties(gif-picker-terminal PROPERTIES TIMEOUT 120)
    find_program(OPENSSL_CLI NAMES openssl REQUIRED)
    add_executable(realtime-probe tests/probes/realtime_probe.c)
    target_link_libraries(realtime-probe PRIVATE papo_auth)
    target_compile_options(realtime-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    papo_add_test(
        NAME
        realtime-integration
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_realtime.py
        $<TARGET_FILE:realtime-probe>
        LABELS
        realtime)
    papo_set_tests_properties(realtime-integration PROPERTIES TIMEOUT 120)
    papo_add_test(
        NAME
        realtime-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_realtime_terminal.py
        $<TARGET_FILE:papo-tui>
        LABELS
        realtime)
    papo_set_tests_properties(realtime-terminal PROPERTIES TIMEOUT 120)
    add_executable(ws-output-test tests/unit/test_ws_output.c)
    target_link_libraries(ws-output-test PRIVATE papo_auth)
    target_compile_options(ws-output-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    papo_add_test(NAME websocket-partial-output COMMAND ws-output-test LABELS realtime)
    find_program(DBUS_RUN_SESSION NAMES dbus-run-session)
    find_program(GDBUS_CLI NAMES gdbus)
    if(PAPO_DESKTOP_SESSIONS
       AND GIO_FOUND
       AND DBUS_RUN_SESSION
       AND GDBUS_CLI)
        add_executable(fake-keyring tests/support/fake_keyring.c)
        target_link_libraries(fake-keyring PRIVATE PkgConfig::GIO PkgConfig::JANSSON)
        target_compile_options(fake-keyring PRIVATE -Wall -Wextra -Wpedantic -Werror)
        papo_add_test(
            NAME
            persistent-sessions
            COMMAND
            ${DBUS_RUN_SESSION}
            --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/support/keyring-bus.conf
            --
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_persistent_sessions.py
            $<TARGET_FILE:papo-tui>
            $<TARGET_FILE:fake-keyring>
            LABELS
            auth
            platform)
        papo_set_tests_properties(persistent-sessions PROPERTIES TIMEOUT 180)
        if(SECRET_FOUND)
            papo_set_tests_properties(persistent-sessions PROPERTIES ENVIRONMENT
                                      "PAPO_TEST_SECRET=1")
        endif()
    endif()

    papo_set_tests_properties(message-interactions-terminal PROPERTIES TIMEOUT 180)
    papo_add_test(
        NAME
        message-mutations-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_message_mutations.py
        $<TARGET_FILE:papo-tui>
        LABELS
        interactions)
    papo_set_tests_properties(message-mutations-terminal PROPERTIES TIMEOUT 180)
    papo_add_test(
        NAME
        history-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_history_terminal.py
        $<TARGET_FILE:papo-tui>
        LABELS
        history)
    papo_set_tests_properties(history-integration history-terminal PROPERTIES TIMEOUT 120)
    papo_add_test(
        NAME
        voice-conversation-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_voice_conversation.py
        $<TARGET_FILE:papo-tui>
        LABELS
        voice)
    papo_set_tests_properties(voice-conversation-terminal PROPERTIES TIMEOUT 120)
    papo_add_test(
        NAME
        message-send-product
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_send_product.py
        $<TARGET_FILE:papo-tui>
        LABELS
        send)
    papo_set_tests_properties(message-send-product PROPERTIES TIMEOUT 120)
    add_executable(send-terminal-probe tests/probes/send_terminal_probe.c)
    target_link_libraries(send-terminal-probe PRIVATE papo_send_editor)
    target_compile_options(send-terminal-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    papo_add_test(
        NAME
        message-send-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_send_terminal.py
        $<TARGET_FILE:send-terminal-probe>
        LABELS
        send)
    papo_set_tests_properties(message-send-terminal PROPERTIES TIMEOUT 120)

    papo_set_tests_properties(message-send-integration PROPERTIES TIMEOUT 120)
    papo_add_test(
        NAME
        attachments-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_attachments.py
        $<TARGET_FILE:papo-tui>
        LABELS
        send)
    papo_set_tests_properties(attachments-terminal PROPERTIES TIMEOUT 120)
    papo_add_test(
        NAME
        downloads-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_downloads.py
        $<TARGET_FILE:papo-tui>
        LABELS
        media)
    papo_set_tests_properties(downloads-terminal PROPERTIES TIMEOUT 120)
    add_executable(attachment-files-test tests/unit/test_attachment_files.c)
    target_link_libraries(attachment-files-test PRIVATE papo_auth)
    target_compile_options(attachment-files-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    papo_add_test(
        NAME
        attachment-files
        COMMAND
        attachment-files-test
        LABELS
        send
        platform)
    if(NOT WIN32)
        add_executable(file-portal-test tests/unit/test_file_portal.c)
        target_link_libraries(file-portal-test PRIVATE papo_send_editor)
        target_compile_options(file-portal-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
        papo_add_test(
            NAME
            file-portal
            COMMAND
            file-portal-test
            LABELS
            send
            platform)
        papo_set_tests_properties(file-portal PROPERTIES TIMEOUT 20)
    endif()

    papo_add_test(
        NAME
        session-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_session_terminal.py
        $<TARGET_FILE:papo-tui>
        LABELS
        auth)
    papo_set_tests_properties(session-integration session-terminal PROPERTIES TIMEOUT 120)

    add_executable(profiles-probe tests/probes/profiles_probe.c)
    target_link_libraries(profiles-probe PRIVATE papo_auth)
    target_compile_options(profiles-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    papo_add_test(
        NAME
        instance-profiles
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_profiles.py
        $<TARGET_FILE:profiles-probe>
        LABELS
        servers)
    papo_add_test(
        NAME
        profiles-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_profiles_terminal.py
        $<TARGET_FILE:papo-tui>
        LABELS
        servers)
    papo_set_tests_properties(instance-profiles profiles-terminal PROPERTIES TIMEOUT 120)
    add_executable(ws-probe tests/probes/ws_probe.c)
    target_link_libraries(ws-probe PRIVATE papo_auth)
    target_compile_options(ws-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)

    papo_add_test(
        NAME
        login-terminal
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_terminal.py
        $<TARGET_FILE:papo-tui>
        LABELS
        auth
        ui)
    add_executable(
        text-test tests/unit/test_text.c src/ui/input/text.c src/ui/input/mouse.c
                  src/model/text/mentions.c src/config/key_binding.c src/model/text/translation.c)
    target_include_directories(text-test PRIVATE src)
    target_link_libraries(text-test PRIVATE PkgConfig::GLIB PkgConfig::NCURSES PkgConfig::UNIBREAK
                                            PkgConfig::JANSSON)
    target_compile_options(text-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(PAPO_SANITIZERS)
        target_compile_options(text-test PRIVATE -fsanitize=address,undefined
                                                 -fno-omit-frame-pointer)
        target_link_options(text-test PRIVATE -fsanitize=address,undefined)
    endif()
    papo_add_test(
        NAME
        unicode-editor
        COMMAND
        text-test
        LABELS
        text
        input)
    papo_add_test(
        NAME
        websocket-integration
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_ws.py
        $<TARGET_FILE:ws-probe>
        LABELS
        realtime)
    papo_set_tests_properties(websocket-integration PROPERTIES TIMEOUT 120)
    papo_add_test(
        NAME
        api-contract
        COMMAND
        ${Python3_EXECUTABLE}
        -m
        unittest
        discover
        -s
        tests/contract
        -p
        test_contract.py
        LABELS
        contract)
    papo_set_tests_properties(api-contract PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                              TIMEOUT 30)
    papo_set_tests_properties(auth-integration login-terminal PROPERTIES TIMEOUT 45)
endif()

if(BUILD_TESTING)

    papo_add_test(
        NAME
        terminal-synchronization
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal/test_terminal_synchronization.py
        $<TARGET_FILE:papo-tui>
        LABELS
        input
        harness)
    papo_set_tests_properties(
        terminal-synchronization PROPERTIES TIMEOUT 240 ENVIRONMENT
        "PAPO_TEST_FFMPEG=${FFMPEG_CLI};PAPO_TEST_MODAL_PROBE=$<TARGET_FILE:media-modal-probe>")

endif()

if(BUILD_TESTING)
    papo_finalize_tests()
endif()
