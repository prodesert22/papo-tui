#!/usr/bin/env python3
"""Build locked dependencies in a private prefix; never overwrite system libraries."""

import argparse
import shlex
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import urllib.request
from voice_dependencies import (
    cmake_options as voice_cmake_options,
    meson_options as voice_meson_options,
)

ROOT = Path(__file__).resolve().parents[1]

# Homebrew formulae installed outside its prefix ("keg-only"): pkg-config does not see them
# until their own pkgconfig directory is added.
MACOS_KEGS = ("openssl@3", "zlib", "libffi")
# Keg-only tools that replace older copies in macOS: GStreamer needs bison 2.4 or later, and macOS
# ships 2.3.
MACOS_TOOL_KEGS = ("bison",)


def host_platform():
    """Name that a source's "platforms" list uses for this machine: linux, macos or nt."""
    if os.name == "nt":
        return "nt"
    return "macos" if sys.platform == "darwin" else "linux"


def builds_here(entry):
    """Whether a locked source is built for this machine."""
    return host_platform() in entry.get("platforms", [host_platform()])


def macos_keg(formula):
    """Install prefix of a Homebrew formula, or None when Homebrew or the formula is missing."""
    try:
        text = subprocess.check_output(
            ["brew", "--prefix", formula], text=True, stderr=subprocess.DEVNULL
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return None
    return text if text and Path(text).is_dir() else None


def macos_pkg_config_dirs():
    """pkgconfig directories of the keg-only Homebrew formulae; empty outside macOS."""
    if host_platform() != "macos":
        return []
    directories = []
    for formula in MACOS_KEGS:
        keg = macos_keg(formula)
        if keg and (Path(keg) / "lib/pkgconfig").is_dir():
            directories.append(str(Path(keg) / "lib/pkgconfig"))
    return directories


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download(url, digest, path):
    if path.exists() and sha256(path) == digest:
        return path
    temporary = path.with_name(path.name + ".part")
    try:
        with urllib.request.urlopen(url, timeout=120) as response, temporary.open("wb") as output:
            shutil.copyfileobj(response, output)
        if sha256(temporary) != digest:
            raise RuntimeError(f"Checksum mismatch: {path.name}")
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)
    return path


def unpack(archive, destination):
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as source:
        for member in source.getmembers():
            name = Path(member.name)
            if name.is_absolute() or ".." in name.parts:
                raise RuntimeError("Unsafe archive path")
            if member.issym() or member.islnk():
                target = (destination / name.parent / member.linkname).resolve()
                if not target.is_relative_to(destination.resolve()):
                    raise RuntimeError("Unsafe archive link")
        if hasattr(tarfile, "data_filter"):
            source.extractall(destination, filter="data")
        else:
            source.extractall(destination)
    directories = [p for p in destination.iterdir() if p.is_dir()]
    if len(directories) != 1:
        raise RuntimeError(f"Expected one source directory in {archive}")
    return directories[0]


def unix_path(path):
    if os.name == "nt":
        return subprocess.check_output(["cygpath", "-u", str(path)], text=True).strip()
    return str(path)


def ncurses_options(prefix, windows=False):
    options = [
        "--prefix=" + prefix,
        "--libdir=" + prefix + "/lib",
        "--with-shared",
        "--without-normal",
        "--without-debug",
        "--enable-widec",
        "--enable-ext-colors",
        "--enable-ext-mouse",
        "--enable-sp-funcs",
        "--enable-pc-files",
        "--without-cxx",
        "--without-ada",
        "--without-tests",
        "--without-progs",
        "--without-manpages",
        "--disable-db-install",
        "--with-pkg-config-libdir=" + prefix + "/lib/pkgconfig",
    ]
    if windows:
        # The bundled MSYS2 terminfo uses hex directories (78/xterm-256color).
        # Without this, a build on a case-sensitive disk looks in x/ instead.
        options += [
            "--enable-term-driver",
            "--enable-interop",
            "--disable-home-terminfo",
            "--disable-mixed-case",
        ]
    else:
        # Use the distro's terminal database while keeping the library private.
        options += [
            "--disable-pkg-ldflags",
            "--with-default-terminfo-dir=/usr/share/terminfo",
            "--with-terminfo-dirs=/etc/terminfo:/lib/terminfo:/usr/share/terminfo",
        ]
    return options


def relocate_ncurses_pc(prefix):
    """Keep ncurses' MSYS install paths out of native CMake include flags."""
    installed = unix_path(prefix)
    for name in ("ncursesw", "formw", "menuw", "panelw"):
        path = prefix / "lib/pkgconfig" / (name + ".pc")
        text = path.read_text().replace(installed, "${prefix}")
        text = text.replace("prefix=${prefix}\n", "prefix=" + prefix.as_posix() + "\n", 1)
        for option, variable in (
            ("-I", "includedir"),
            ("-I", "prefix"),
            ("-L", "libdir"),
            ("-L", "prefix"),
        ):
            reference = "${" + variable + "}"
            text = text.replace(option + reference, option + '"' + reference + '"')
        path.write_text(text)


def patch_windows_libsixel(source):
    """Use a Windows timer for libsixel's POSIX nanosleep calls."""
    encoder = source / "src/encoder.c"
    code = encoder.read_text()
    marker = '#include "rgblookup.h"'
    if "#define nanosleep sixel_windows_nanosleep" in code:
        return
    if marker not in code:
        raise RuntimeError("Cannot apply the Windows libsixel nanosleep compatibility patch")
    compatibility = r"""

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef nanosleep
#undef nanosleep
#endif
static int
sixel_windows_nanosleep(const struct timespec *request, struct timespec *remaining)
{
    if (request == NULL || request->tv_sec < 0 || request->tv_nsec < 0 ||
        request->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -1;
    }

    uint64_t seconds = (uint64_t)request->tv_sec;
    uint64_t nanoseconds = (uint64_t)request->tv_nsec;
    if (seconds > (UINT64_MAX - nanoseconds) / 1000000000ULL) {
        errno = EINVAL;
        return -1;
    }

    HANDLE timer = CreateWaitableTimer(NULL, TRUE, NULL);
    if (timer == NULL) {
        errno = EFAULT;
        return -1;
    }
    LARGE_INTEGER due_time;
    due_time.QuadPart = -(LONGLONG)((seconds * 1000000000ULL + nanoseconds) / 100ULL);
    if (!SetWaitableTimer(timer, &due_time, 0, NULL, NULL, FALSE)) {
        CloseHandle(timer);
        errno = EFAULT;
        return -1;
    }
    DWORD result = WaitForSingleObject(timer, INFINITE);
    CloseHandle(timer);
    if (result != WAIT_OBJECT_0) {
        errno = EFAULT;
        return -1;
    }
    if (remaining != NULL) {
        remaining->tv_sec = 0;
        remaining->tv_nsec = 0;
    }
    return 0;
}
#define nanosleep sixel_windows_nanosleep
#endif
"""
    encoder.write_text(code.replace(marker, marker + compatibility, 1))


def build(entry, source, work, prefix, jobs, env):
    def run(*args, cwd=source):
        print("+", *map(str, args), flush=True)
        subprocess.run(list(map(str, args)), cwd=cwd, env=env, check=True)

    name = entry["name"]
    binary = work / "build"
    if name == "webrtc-audio-processing":
        run(
            "patch",
            "-p1",
            "--forward",
            "--input",
            unix_path(ROOT / "dist/patches/webrtc-audio-processing-cstdint.patch"),
        )
    if name == "abseil-cpp" and os.name == "nt":
        # MinGW's roapi.h does not include the HSTRING functions abseil calls.
        run(
            "patch",
            "-p1",
            "--forward",
            "--input",
            unix_path(ROOT / "dist/patches/abseil-cpp-mingw-winstring.patch"),
        )
    if entry["buildsystem"] == "cmake":
        options = [
            "-DBUILD_SHARED_LIBS=ON",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_INSTALL_LIBDIR=lib",
            "-DCMAKE_INSTALL_PREFIX=" + str(prefix),
            "-DCMAKE_PREFIX_PATH=" + str(prefix),
            "-DBUILD_TESTING=OFF",
        ]
        if name == "curl":
            options += [
                "-DBUILD_CURL_EXE=OFF",
                "-DCURL_DISABLE_WEBSOCKETS=OFF",
                "-DENABLE_THREADED_RESOLVER=ON",
                "-DCURL_USE_LIBPSL=OFF",
                "-DCURL_BROTLI=OFF",
                "-DCURL_ZSTD=OFF",
                "-DCURL_USE_LIBSSH2=OFF",
            ]
            options += (
                ["-DCURL_USE_SCHANNEL=ON", "-DCURL_USE_OPENSSL=OFF"]
                if os.name == "nt"
                else ["-DCURL_USE_OPENSSL=ON"]
            )
            if host_platform() == "macos":
                # Apple's curl dropped Secure Transport; the keg-only OpenSSL is not on the default path.
                openssl = macos_keg("openssl@3")
                if openssl:
                    options += ["-DOPENSSL_ROOT_DIR=" + openssl]
        elif name == "jansson":
            options += [
                "-DJANSSON_BUILD_SHARED_LIBS=ON",
                "-DJANSSON_BUILD_DOCS=OFF",
                "-DJANSSON_WITHOUT_TESTS=ON",
            ]
        options += voice_cmake_options(name)
        run("cmake", "-S", source, "-B", binary, "-G", "Ninja", *options)
        run("cmake", "--build", binary, "--parallel", jobs)
        run("cmake", "--install", binary)
    elif entry["buildsystem"] == "autotools":
        if not (source / "configure").exists():
            run("autoreconf", "-fi")
        binary.mkdir(exist_ok=True)
        options = [
            "--prefix=" + unix_path(prefix),
            "--libdir=" + unix_path(prefix / "lib"),
            "--enable-shared",
            "--disable-static",
        ]
        if name == "chafa":
            options += ["--without-imagemagick", "--without-librsvg"]
        elif name == "ncurses":
            if os.name == "nt":
                # Use the TUI's existing GLib width policy on Windows.
                env = dict(env)
                for option, variable in (("--cflags", "CPPFLAGS"), ("--libs", "LIBS")):
                    flags = subprocess.check_output(
                        ["pkg-config", option, "glib-2.0"], env=env, text=True
                    ).strip()
                    env[variable] = (flags + " " + env.get(variable, "")).strip()
                # Wide input first; then UTF-16 surrogate pairs for emoji output.
                for patch in ("ncurses-win32-wide-input.patch", "ncurses-win32-surrogates.patch"):
                    run(
                        "patch",
                        "-p1",
                        "--forward",
                        "--input",
                        unix_path(ROOT / "dist/patches" / patch),
                    )
            options = ncurses_options(unix_path(prefix), windows=os.name == "nt")
        run("sh", unix_path(source / "configure"), *options, cwd=binary)
        run("make", "-j", jobs, cwd=binary)
        run("make", "install", cwd=binary)
        if name == "ncurses" and os.name == "nt":
            relocate_ncurses_pc(prefix)
    elif entry["buildsystem"] == "ffmpeg":
        binary.mkdir(exist_ok=True)
        options = [
            "--prefix=" + unix_path(prefix),
            "--libdir=" + unix_path(prefix / "lib"),
            "--enable-shared",
            "--disable-static",
            "--disable-programs",
            "--disable-doc",
            "--disable-debug",
            "--disable-gpl",
            "--disable-nonfree",
            "--disable-autodetect",
            # Only the protocols the player uses: local files and the loopback relay that streams
            # an attachment while it downloads (video_player.c allows nothing else).
            "--disable-protocols",
            "--enable-protocol=file,http,tcp",
            "--enable-zlib",
        ]
        if os.name == "nt":
            options += ["--target-os=mingw32", "--cc=clang", "--cxx=clang++", "--enable-d3d11va"]
            if os.environ.get("MSYSTEM") == "CLANGARM64":
                options += ["--arch=aarch64"]
            else:
                options += ["--arch=x86_64"]
        elif host_platform() == "macos":
            # --disable-autodetect leaves the system frameworks out unless they are named.
            options += ["--enable-videotoolbox", "--enable-audiotoolbox"]
        else:
            options += ["--enable-vaapi"]
        run("sh", unix_path(source / "configure"), *options, cwd=binary)
        run("make", "-j", jobs, cwd=binary)
        run("make", "install", cwd=binary)
    else:
        options = [
            # mpv embeds Meson's command line in a C string without escaping
            # Windows backslashes (C:\Users becomes an invalid \U escape).
            "--prefix=" + prefix.as_posix(),
            "--libdir=lib",
            "--buildtype=release",
            "--default-library=shared",
            "--auto-features=disabled",
            "--wrap-mode=nodownload",
        ]
        if name == "mpv":
            options += [
                "-Dgpl=false",
                "-Dlibmpv=true",
                "-Dcplayer=false",
                "-Dgl=disabled",
                "-Dbuild-date=false",
            ]
            if os.name == "nt":
                options += ["-Dwasapi=enabled", "-Dwin32-threads=enabled", "-Dd3d-hwaccel=enabled"]
            elif host_platform() == "macos":
                options += ["-Dcoreaudio=enabled"]
            else:
                options += [
                    "-Dpulse=enabled",
                    "-Dalsa=enabled",
                    "-Ddrm=enabled",
                    "-Dvaapi=enabled",
                    "-Dvaapi-drm=enabled",
                ]
        elif name == "libsixel":
            options += ["-Dimg2sixel=disabled", "-Dsixel2png=disabled", "-Dtests=disabled"]
        elif name == "libsecret":
            options += [
                "-Dmanpage=false",
                "-Dvapi=false",
                "-Dgtk_doc=false",
                "-Dintrospection=false",
                "-Dcrypto=libgcrypt",
                "-Dtest_setup=disabled",
            ]
        elif name == "libplacebo":
            if host_platform() == "macos":
                # Apple's libc++ has no floating-point std::from_chars, so libplacebo needs the
                # header-only fast_float; its Vulkan stubs need the Vulkan headers. Homebrew
                # keeps both outside the compiler's default include path.
                includes = " ".join(
                    "-I" + keg + "/include"
                    for keg in map(macos_keg, ("fast_float", "vulkan-headers"))
                    if keg
                )
                env = dict(env)
                for variable in ("CFLAGS", "CXXFLAGS"):
                    env[variable] = (includes + " " + env.get(variable, "")).strip()
            options += [
                "-Dvulkan=disabled",
                "-Dshaderc=disabled",
                "-Dglslang=disabled",
                "-Dopengl=disabled",
                "-Dtests=false",
                "-Ddemos=false",
            ]
        elif name == "libass":
            options += ["-Dfontconfig=enabled", "-Dlibunibreak=enabled", "-Dasm=auto"]
        options += voice_meson_options(
            name, windows=os.name == "nt", macos=host_platform() == "macos"
        )
        run("meson", "setup", binary, source, *options)
        run("meson", "compile", "-C", binary, "-j", jobs)
        run("meson", "install", "-C", binary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--cache", type=Path, default=ROOT / "build/source-cache")
    parser.add_argument("--only", help="comma-separated source names (default: all)")
    parser.add_argument("--jobs", default=str(min(os.cpu_count() or 2, 4)))
    args = parser.parse_args()
    prefix = args.prefix.resolve()
    prefix.mkdir(parents=True, exist_ok=True)
    cache = args.cache.resolve()
    cache.mkdir(parents=True, exist_ok=True)
    lock = json.loads((ROOT / "dist/dependencies.json").read_text())
    env = dict(os.environ)
    env["PKG_CONFIG_PATH"] = os.pathsep.join(
        [
            str(prefix / "lib/pkgconfig"),
            str(prefix / "lib64/pkgconfig"),
            *macos_pkg_config_dirs(),
            env.get("PKG_CONFIG_PATH", ""),
        ]
    )
    env["PATH"] = str(prefix / "bin") + os.pathsep + env["PATH"]
    if host_platform() == "macos":
        tools = [macos_keg(formula) for formula in MACOS_TOOL_KEGS]
        env["PATH"] = os.pathsep.join([*(keg + "/bin" for keg in tools if keg), env["PATH"]])
    if os.name != "nt":
        env["LDFLAGS"] = (
            "-L"
            + str(prefix / "lib")
            + " -Wl,-rpath,"
            + str(prefix / "lib")
            + " "
            + env.get("LDFLAGS", "")
        )
    if host_platform() == "macos":
        # dist/macos_bundle.py rewrites library names in place; leave room in the headers.
        env["LDFLAGS"] = "-Wl,-headerpad_max_install_names " + env["LDFLAGS"]
    selected = set(args.only.split(",")) if args.only else {x["name"] for x in lock["sources"]}
    unknown = selected - {x["name"] for x in lock["sources"]}
    if unknown:
        parser.error(f"Unknown sources: {sorted(unknown)}")
    recipe = sha256(Path(__file__)) + sha256(ROOT / "dist/voice_dependencies.py")
    compiler = subprocess.check_output(
        [*shlex.split(os.environ.get("CC", "clang" if os.name == "nt" else "cc")), "--version"],
        text=True,
    ).splitlines()[0]
    for entry in lock["sources"]:
        if entry["name"] not in selected:
            continue
        if not builds_here(entry):
            continue
        stamp = prefix / (".papo-" + entry["name"] + ".json")
        identity = {
            "source": entry,
            "recipe_sha256": recipe,
            "compiler": compiler,
            "compatibility_patch": (
                "windows-nanosleep-v1"
                if os.name == "nt" and entry["name"] == "libsixel"
                else "windows-wide-input-v1-"
                + sha256(ROOT / "dist/patches/ncurses-win32-wide-input.patch")
                + sha256(ROOT / "dist/patches/ncurses-win32-surrogates.patch")
                if os.name == "nt" and entry["name"] == "ncurses"
                else "windows-native-threads-posix-prefix-v1"
                if os.name == "nt" and entry["name"] == "mpv"
                else "mingw-winstring-v1-"
                + sha256(ROOT / "dist/patches/abseil-cpp-mingw-winstring.patch")
                if os.name == "nt" and entry["name"] == "abseil-cpp"
                else "cstdint-v1-"
                + sha256(ROOT / "dist/patches/webrtc-audio-processing-cstdint.patch")
                if entry["name"] == "webrtc-audio-processing"
                else None
            ),
            "system": host_platform(),
            "architecture": os.environ.get(
                "MSYSTEM", os.uname().machine if hasattr(os, "uname") else "windows"
            ),
            "prefix": str(prefix),
        }
        if entry["name"] == "ncurses":
            identity["configure_options"] = ncurses_options(
                unix_path(prefix), windows=os.name == "nt"
            )
        if stamp.exists() and json.loads(stamp.read_text()) == identity:
            if os.name == "nt" and entry["name"] == "ncurses":
                relocate_ncurses_pc(prefix)
            continue
        archive = download(entry["url"], entry["sha256"], cache / entry["archive"])
        work = prefix.parent / ("dependency-" + entry["name"])
        if work.exists():
            shutil.rmtree(work)
        source = unpack(archive, work / "source")
        if os.name == "nt" and entry["name"] == "libsixel":
            patch_windows_libsixel(source)
        build(entry, source, work, prefix, args.jobs, env)
        # Keep exact sources, build commands/options and upstream licenses available to packaging.
        licenses = prefix / "share/licenses/papo-dependencies" / entry["name"]
        licenses.mkdir(parents=True, exist_ok=True)
        for candidate in source.iterdir():
            if candidate.is_file() and candidate.name.upper().startswith(
                ("COPYING", "LICENSE", "COPYRIGHT", "AUTHORS")
            ):
                shutil.copy2(candidate, licenses / candidate.name)
        if entry["name"] == "gst-plugins-bad":
            shutil.copy2(source / "ext/sctp/usrsctp/LICENSE.md", licenses / "usrsctp-LICENSE.md")
        stamp.write_text(json.dumps(identity, indent=2) + "\n")
    print(prefix)


if __name__ == "__main__":
    main()
