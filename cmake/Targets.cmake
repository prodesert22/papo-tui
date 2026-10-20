if(WIN32)
    set(PAPO_PLATFORM windows)
else()
    set(PAPO_PLATFORM linux)
endif()
add_library(papo_platform src/platform/${PAPO_PLATFORM}/runtime.c)
target_include_directories(papo_platform PUBLIC src)
target_link_libraries(papo_platform PUBLIC PkgConfig::GLIB)
if(WIN32)
    target_sources(papo_platform PRIVATE src/platform/windows/files.c)
    target_link_libraries(papo_platform PRIVATE bcrypt advapi32)
endif()
add_library(
    papo_auth
    src/net/auth/api.c
    src/net/auth/client.c
    src/net/auth/messages.c
    src/model/attachment.c
    src/net/auth/realtime.c
    src/net/auth/responses.c
    src/net/auth/session.c
    src/net/auth/transport.c
    src/net/auth/validation.c
    src/model/permissions.c
    src/net/auth/permissions.c
    src/model/text/mentions.c
    src/model/assets/catalog.c
    src/net/http/download.c
    src/net/auth/session_time.c
    src/net/rate_limit.c
    src/net/realtime/websocket.c
    src/config/app_mode.c
    src/config/player_volume.c
    src/config/profiles.c
    src/platform/${PAPO_PLATFORM}/profiles_storage.c
    src/config/read_state.c
    src/platform/${PAPO_PLATFORM}/read_storage.c
    src/model/conversation/discovery.c
    src/model/conversation/members.c
    src/model/conversation/directory.c
    src/model/conversation/events.c
    src/model/conversation/history.c
    src/model/conversation/interactions.c
    src/model/conversation/pagination.c
    src/model/notifications/inbox.c)
target_include_directories(papo_auth PUBLIC src)
target_link_libraries(papo_auth PUBLIC papo_platform PkgConfig::CURL PkgConfig::JANSSON
                                       PkgConfig::GLIB PkgConfig::GIO)

add_library(papo_outbox src/app/outbox.c)
target_link_libraries(papo_outbox PUBLIC papo_auth PkgConfig::UNIBREAK)
target_compile_options(papo_outbox PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(WIN32)
    set(PAPO_SESSION_SOURCE src/platform/windows/session_store.c)
else()
    set(PAPO_SESSION_SOURCE src/app/session_store.c)
endif()
add_library(papo_sessions ${PAPO_SESSION_SOURCE})
target_link_libraries(papo_sessions PUBLIC papo_auth)
target_compile_options(papo_sessions PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(WIN32)
    target_link_libraries(papo_sessions PRIVATE PkgConfig::GIO advapi32)
    if(PAPO_DESKTOP_SESSIONS)
        target_compile_definitions(papo_sessions PRIVATE PAPO_WINDOWS_CREDENTIALS)
    endif()
endif()
if(PAPO_DESKTOP_SESSIONS
   AND GIO_FOUND
   AND NOT WIN32)
    target_compile_definitions(papo_sessions PRIVATE HAVE_GIO)
    target_link_libraries(papo_sessions PRIVATE PkgConfig::GIO)
    if(SECRET_FOUND)
        target_compile_definitions(papo_sessions PRIVATE HAVE_LIBSECRET)
        target_link_libraries(papo_sessions PRIVATE PkgConfig::SECRET)
    endif()
endif()
add_library(papo_desktop src/platform/${PAPO_PLATFORM}/notifications.c)
target_link_libraries(papo_desktop PUBLIC papo_auth)
target_compile_options(papo_desktop PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(PAPO_DESKTOP_NOTIFICATIONS AND GIO_FOUND)
    target_compile_definitions(papo_desktop PRIVATE HAVE_GIO)
    target_link_libraries(papo_desktop PRIVATE PkgConfig::GIO)
endif()

add_library(
    papo_send_editor
    src/model/text/emoji_catalog.c
    src/ui/input/editor.c
    src/ui/components/mention_popup.c
    src/ui/components/theme.c
    src/ui/components/status_bar.c
    src/ui/components/terminal_title.c
    src/ui/input/text.c
    src/ui/input/mouse.c
    src/platform/file_transfer.c
    src/platform/${PAPO_PLATFORM}/file_transfer.c
    src/platform/${PAPO_PLATFORM}/clipboard.c
    src/app/instances.c)
target_link_libraries(
    papo_send_editor PUBLIC papo_outbox papo_sessions papo_desktop PkgConfig::NCURSES
                            PkgConfig::UNIBREAK PkgConfig::GLIB)
target_compile_options(papo_send_editor PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(WIN32)
    target_link_libraries(papo_send_editor PRIVATE ole32 shell32 uuid windowscodecs)
endif()

add_library(papo_image_codec MODULE src/ui/media/image_codec.c)
target_include_directories(papo_image_codec PRIVATE src)
target_link_libraries(
    papo_image_codec
    PRIVATE PkgConfig::NCURSES
            PkgConfig::GMODULE
            PkgConfig::AVFORMAT
            PkgConfig::AVCODEC
            PkgConfig::AVUTIL
            PkgConfig::SWSCALE
            m)
add_library(papo_video_player MODULE src/ui/media/video_player.c)
target_include_directories(papo_video_player PRIVATE src)
target_link_libraries(papo_video_player PRIVATE PkgConfig::NCURSES PkgConfig::GMODULE
                                                PkgConfig::MPV)
foreach(target papo_image_codec papo_video_player)
    set_target_properties(
        ${target}
        PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/papo-backends"
                   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/papo-backends"
                   C_VISIBILITY_PRESET hidden)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(PAPO_SANITIZERS)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined
                                                 -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endforeach()

add_executable(
    papo-media-worker
    src/ui/media/media_service_worker.c
    src/ui/media/media_wire.c
    src/ui/media/media_backend.c
    src/ui/media/graphics_encode.c
    src/ui/media/graphics_buffer.c
    src/ui/media/sixel_encode.c
    src/ui/media/image_decode.c
    src/ui/media/emoji_raster.c
    src/ui/media/resources.c)
target_include_directories(papo-media-worker PRIVATE src)
target_compile_options(papo-media-worker PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(
    papo-media-worker
    PRIVATE papo_platform
            PkgConfig::NCURSES
            PkgConfig::GMODULE
            PkgConfig::PANGO
            PkgConfig::FONTCONFIG
            PkgConfig::CHAFA
            PkgConfig::SIXEL
            m)
target_compile_definitions(
    papo-media-worker
    PRIVATE PAPO_MEDIA_RESOURCES_WORKER PAPO_IMAGE_MODULE="$<TARGET_FILE_NAME:papo_image_codec>"
            PAPO_VIDEO_MODULE="$<TARGET_FILE_NAME:papo_video_player>"
            PAPO_BACKEND_INSTALL_DIR="${CMAKE_INSTALL_FULL_BINDIR}/papo-backends")
set_target_properties(papo-media-worker PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                                   "${CMAKE_CURRENT_BINARY_DIR}/papo-backends")
add_dependencies(papo-media-worker papo_image_codec papo_video_player)
if(PAPO_SANITIZERS)
    target_compile_options(papo-media-worker PRIVATE -fsanitize=address,undefined
                                                     -fno-omit-frame-pointer)
    target_link_options(papo-media-worker PRIVATE -fsanitize=address,undefined)
endif()

add_library(
    papo_media
    src/model/text/emoji_usage.c
    src/ui/media/resources.c
    src/ui/media/media.c
    src/ui/media/media_cache.c
    src/ui/media/media_stream.c
    src/ui/media/media_process.c
    src/ui/media/media_service.c
    src/ui/media/media_wire.c
    src/ui/media/graphics_buffer.c
    src/ui/media/media_layout.c
    src/ui/media/message_time.c
    src/ui/media/emoji_image.c
    src/ui/media/icons.c
    src/ui/components/emoji_picker.c
    src/ui/media/image_decode.c
    src/ui/media/graphics.c
    src/ui/media/sixel.c
    src/ui/media/message_blocks.c)
target_link_libraries(papo_media PUBLIC papo_send_editor PkgConfig::GMODULE PkgConfig::GIO
                                        Threads::Threads m)
target_compile_definitions(
    papo_media
    PRIVATE PAPO_MEDIA_WORKER_FILE="$<TARGET_FILE_NAME:papo-media-worker>"
            PAPO_IMAGE_MODULE="$<TARGET_FILE_NAME:papo_image_codec>"
            PAPO_VIDEO_MODULE="$<TARGET_FILE_NAME:papo_video_player>"
            PAPO_BACKEND_INSTALL_DIR="${CMAKE_INSTALL_FULL_BINDIR}/papo-backends")
add_dependencies(papo_media papo-media-worker)
target_compile_options(papo_media PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(
    papo-tui
    src/main.c
    src/ui/screens/login.c
    src/platform/${PAPO_PLATFORM}/terminal.c
    src/ui/screens/conversation/conversation.c
    src/ui/screens/conversation/drawing.c
    src/ui/screens/conversation/input.c
    src/ui/screens/conversation/attachments.c
    src/ui/screens/conversation/interactions.c
    src/ui/screens/conversation/reactions.c
    src/ui/screens/conversation/members.c
    src/ui/screens/conversation/layout.c
    src/ui/components/settings.c
    src/ui/screens/message_browser.c
    src/ui/screens/inbox.c
    src/ui/screens/profiles.c
    src/ui/components/button.c)
target_sources(papo-tui PRIVATE src/ui/components/file_picker.c)
target_compile_definitions(papo-tui
                           PRIVATE PAPO_NATIVE_FILE_DIALOGS=$<BOOL:${PAPO_NATIVE_FILE_DIALOGS}>)
target_link_libraries(papo-tui PRIVATE papo_media PkgConfig::FORM PkgConfig::NCURSES
                                       PkgConfig::UNIBREAK)

foreach(target papo_auth papo-tui)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
endforeach()
if(PAPO_SANITIZERS)
    target_compile_options(papo_auth PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(papo_auth PUBLIC -fsanitize=address,undefined)
endif()
