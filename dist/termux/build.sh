# shellcheck shell=bash disable=SC2034
TERMUX_PKG_HOMEPAGE=https://github.com/prodesert22/papo-tui
TERMUX_PKG_DESCRIPTION="Terminal chat client for independent Papo instances"
TERMUX_PKG_LICENSE=MIT
TERMUX_PKG_MAINTAINER="Papo TUI contributors"
TERMUX_PKG_VERSION=0.5.0
TERMUX_PKG_SRCURL="https://codeload.github.com/prodesert22/papo-tui/tar.gz/refs/tags/v${TERMUX_PKG_VERSION}"
TERMUX_PKG_SHA256=09dcaf03311db7d10e387c1fb39991260f9c1fb0c3ccf792383dce2ca8a38ad0
TERMUX_PKG_DEPENDS="ca-certificates, chafa, ffmpeg, fontconfig, glib, libcurl, libjansson, libsixel, libunibreak, mpv, ncurses, ncurses-ui-libs, pango"
TERMUX_PKG_BUILD_DEPENDS="aosp-libs"
# getrandom() is provided by Android's libc starting at API 28 (Android 9).
TERMUX_PKG_API_LEVEL=28
TERMUX_PKG_EXTRA_CONFIGURE_ARGS="
-DPAPO_RELEASE_BUILD=ON
-DPAPO_BUNDLE_RUNTIME=OFF
-DPAPO_NATIVE_FILE_DIALOGS=OFF
-DPAPO_DESKTOP_SESSIONS=OFF
-DPAPO_DESKTOP_NOTIFICATIONS=OFF
-DPAPO_CURL_WS_SUPPORTED=TRUE
-DBUILD_TESTING=OFF
"

termux_step_pre_configure() {
    # Termux rolls ncurses back to a patched 6.5 snapshot in its 6.6 package.
    # The 6.6 Windows console fix is not needed on Android. Keep this exception
    # in the Termux recipe rather than lowering desktop dependency requirements.
    sed -i 's/ncursesw>=6.6/ncursesw>=6.5/;s/formw>=6.6/formw>=6.5/' \
        "$TERMUX_PKG_SRCDIR/cmake/Dependencies.cmake"
    python3 "$TERMUX_PKG_SRCDIR/dist/resources.py"
}

termux_step_post_make_install() {
    if [ "$TERMUX_ON_DEVICE_BUILD" = "true" ]; then
        "$TERMUX_PREFIX/bin/papo-tui" --version
        "$TERMUX_PREFIX/bin/papo-tui" --help
    else
        termux_setup_proot
        termux-proot-run "$TERMUX_PREFIX/bin/papo-tui" --version
        termux-proot-run "$TERMUX_PREFIX/bin/papo-tui" --help
    fi
}
