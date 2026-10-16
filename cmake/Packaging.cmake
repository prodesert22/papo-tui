include(InstallRequiredSystemLibraries)
set(PAPO_PLATFORM_DATA_PREFIX
    ""
    CACHE PATH "Platform prefix containing Windows runtime data")
if(WIN32 AND PAPO_PLATFORM_DATA_PREFIX)
    # GIO uses these helpers to create redirected subprocess pipes on Windows.
    install(PROGRAMS "${PAPO_PLATFORM_DATA_PREFIX}/bin/gspawn-win64-helper.exe"
                     "${PAPO_PLATFORM_DATA_PREFIX}/bin/gspawn-win64-helper-console.exe"
            DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY "${PAPO_PLATFORM_DATA_PREFIX}/share/terminfo"
            DESTINATION ${CMAKE_INSTALL_DATADIR})
    install(DIRECTORY "${PAPO_PLATFORM_DATA_PREFIX}/etc/fonts" DESTINATION etc)
endif()
set(PAPO_DEPENDENCY_LICENSE_DIR
    ""
    CACHE PATH "Licenses from the locked dependency prefix")
if(IS_DIRECTORY "${PAPO_DEPENDENCY_LICENSE_DIR}")
    install(DIRECTORY "${PAPO_DEPENDENCY_LICENSE_DIR}/"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/papo-tui/dependencies)
endif()
install(TARGETS papo-tui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(
    TARGETS papo_image_codec papo_video_player papo-media-worker
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}/papo-backends
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/papo-backends)
install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/papo-tui)
install(
    FILES data/emoji/LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/papo-tui
    RENAME Unicode-LICENSE)
install(FILES README.md docs/uso.md docs/instalacao.md docs/builds.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
if(UNIX)
    install(FILES dist/io.github.prodesert22.PapoTUI.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES dist/io.github.prodesert22.PapoTUI.metainfo.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    install(FILES dist/io.github.prodesert22.PapoTUI.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
set(PAPO_RESOURCE_DIR
    "${PROJECT_SOURCE_DIR}/build/resources"
    CACHE PATH "Verified release font resources")
if(EXISTS "${PAPO_RESOURCE_DIR}/NotoColorEmoji.ttf")
    install(FILES "${PAPO_RESOURCE_DIR}/NotoColorEmoji.ttf"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/fonts/papo-tui)
    install(
        FILES "${PAPO_RESOURCE_DIR}/fonts/LICENSE"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/papo-tui
        RENAME Noto-OFL.txt)
endif()
option(PAPO_BUNDLE_RUNTIME "Include non-system runtime libraries in the installation" OFF)
if(PAPO_BUNDLE_RUNTIME)
    configure_file(${PROJECT_SOURCE_DIR}/cmake/BundleRuntime.cmake.in
                   ${PROJECT_BINARY_DIR}/BundleRuntime.in @ONLY)
    file(
        GENERATE
        OUTPUT ${PROJECT_BINARY_DIR}/BundleRuntime.cmake
        INPUT ${PROJECT_BINARY_DIR}/BundleRuntime.in)
    install(SCRIPT ${PROJECT_BINARY_DIR}/BundleRuntime.cmake)
endif()
set(CPACK_PACKAGE_NAME papo-tui)
set(CPACK_PACKAGE_VENDOR "Papo TUI contributors")
set(CPACK_PACKAGE_CONTACT "Papo TUI contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Cliente de conversas para terminal")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_DIRECTORY "${PROJECT_BINARY_DIR}/packages")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION net)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "fonts-noto-color-emoji")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE MIT)
set(CPACK_RPM_PACKAGE_GROUP "Applications/Internet")
set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
set(CPACK_RPM_PACKAGE_RECOMMENDS "google-noto-color-emoji-fonts")
if(PAPO_BUNDLE_RUNTIME AND UNIX)
    execute_process(
        COMMAND getconf GNU_LIBC_VERSION
        OUTPUT_VARIABLE PAPO_GLIBC
        OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    string(REGEX REPLACE "^glibc " "" PAPO_GLIBC "${PAPO_GLIBC}")
    # Private copies must not advertise system-wide shared-library providers.
    # GET_RUNTIME_DEPENDENCIES verifies the complete non-glibc closure.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libc6 (>= ${PAPO_GLIBC}), ca-certificates, ncurses-base, fontconfig-config, libasound2-data"
    )
    set(CPACK_RPM_PACKAGE_AUTOREQPROV OFF)
    set(CPACK_RPM_PACKAGE_REQUIRES
        "glibc >= ${PAPO_GLIBC}, ca-certificates, ncurses-base, fontconfig, alsa-lib")
endif()
set(CPACK_SOURCE_GENERATOR TGZ)
set(CPACK_SOURCE_IGNORE_FILES
    "/build/;/[.]git/;/[.]worktrees/;/[.]roadmap/;__pycache__;[.]env($|[.]);[.]cookiejar$")
include(CPack)
