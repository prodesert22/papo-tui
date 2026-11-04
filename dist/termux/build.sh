# shellcheck shell=bash disable=SC2034
TERMUX_PKG_HOMEPAGE=https://github.com/prodesert22/papo-tui
TERMUX_PKG_DESCRIPTION="Terminal chat client for independent Papo instances"
TERMUX_PKG_LICENSE=MIT
TERMUX_PKG_MAINTAINER="Papo TUI contributors"
TERMUX_PKG_VERSION=0.5.0
TERMUX_PKG_SRCURL="https://codeload.github.com/prodesert22/papo-tui/tar.gz/refs/tags/v${TERMUX_PKG_VERSION}"
TERMUX_PKG_SHA256=09dcaf03311db7d10e387c1fb39991260f9c1fb0c3ccf792383dce2ca8a38ad0
# libunibreak is deliberately absent: the Unicode segmentation library is built
# from the locked source and linked statically, so the package installs from
# every Termux repository instead of requiring an unpublished package.
# The GStreamer WebRTC stack is not published for Android either, so the
# package is built without voice calls.
TERMUX_PKG_DEPENDS="ca-certificates, chafa, ffmpeg, fontconfig, glib, libcurl, libjansson, libsixel, mpv, ncurses, ncurses-ui-libs, pango, librsvg"
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
-DPAPO_VOICE=OFF
-DBUILD_TESTING=OFF
"

# Build the Unicode segmentation library as a static archive in a private
# directory. The source, version and checksum come from dist/dependencies.json,
# so the Termux package uses the same reviewed dependency as the desktop
# builds.
termux_step_build_static_unibreak() {
    local lock="$TERMUX_PKG_SRCDIR/dist/dependencies.json"
    local archive="$TERMUX_PKG_CACHEDIR/libunibreak.tar.gz"
    local source="$TERMUX_PKG_CACHEDIR/libunibreak"
    local prefix="$TERMUX_PKG_CACHEDIR/libunibreak-static"
    local url sha256
    read -r url sha256 <<< "$(python3 - "$lock" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as handle:
    sources = json.load(handle)["sources"]
entry = next(item for item in sources if item["name"] == "libunibreak")
print(entry["url"], entry["sha256"])
PY
)"
    termux_download "$url" "$archive" "$sha256"
    rm -rf "$source" "$prefix"
    mkdir -p "$source" "$prefix"
    tar -xzf "$archive" -C "$source" --strip-components=1
    (
        cd "$source" || exit 1
        CFLAGS="-fPIC $CFLAGS" ./configure \
            --host="$TERMUX_HOST_PLATFORM" \
            --prefix="$prefix" \
            --libdir="$prefix/lib" \
            --disable-shared \
            --enable-static
        make -j "$TERMUX_PKG_MAKE_PROCESSES"
        make install
    )
    # The static archive replaces the shared library: the installed program no
    # longer records a libunibreak.so dependency.
    export PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$PKG_CONFIG_LIBDIR"
    termux_setup_pkg_config_wrapper "$prefix/lib/pkgconfig"
    # CMake only searches libraries inside CMAKE_FIND_ROOT_PATH, so the private
    # prefix is added to it. Otherwise the shared library of the build
    # environment is selected instead of the static archive.
    TERMUX_PKG_EXTRA_CONFIGURE_ARGS+=" -DCMAKE_FIND_ROOT_PATH=$TERMUX_PREFIX;$prefix"
}

termux_step_pre_configure() {
    # Termux rolls ncurses back to a patched 6.5 snapshot in its 6.6 package.
    # The 6.6 Windows console fix is not needed on Android. Keep this exception
    # in the Termux recipe rather than lowering desktop dependency requirements.
    sed -i 's/ncursesw>=6.6/ncursesw>=6.5/;s/formw>=6.6/formw>=6.5/' \
        "$TERMUX_PKG_SRCDIR/cmake/Dependencies.cmake"
    python3 "$TERMUX_PKG_SRCDIR/dist/resources.py"
    termux_step_build_static_unibreak
}

termux_step_post_make_install() {
    # The statically linked Unicode segmentation library keeps its own license.
    install -Dm644 "$TERMUX_PKG_CACHEDIR/libunibreak/LICENCE" \
        "$TERMUX_PREFIX/share/doc/papo-tui/libunibreak-LICENCE"
    if [ "$TERMUX_ON_DEVICE_BUILD" = "true" ]; then
        "$TERMUX_PREFIX/bin/papo-tui" --version
        "$TERMUX_PREFIX/bin/papo-tui" --help
    else
        termux_setup_proot
        termux-proot-run "$TERMUX_PREFIX/bin/papo-tui" --version
        termux-proot-run "$TERMUX_PREFIX/bin/papo-tui" --help
    fi
}
