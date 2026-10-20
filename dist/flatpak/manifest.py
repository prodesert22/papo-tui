#!/usr/bin/env python3
"""Generate a Flatpak manifest from the same locked sources as native builds."""

import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from bootstrap import ROOT, ncurses_options, sha256

APP_ID = "io.github.prodesert22.PapoTUI"
RUNTIME = "26.08"


def module(entry):
    name = entry["name"]
    result = {
        "name": name,
        "sources": [
            {
                "type": "archive",
                "url": entry["url"],
                "sha256": entry["sha256"],
                "dest-filename": entry["archive"],
            }
        ],
    }
    if entry["buildsystem"] == "python-package":
        result["buildsystem"] = "simple"
        result["build-commands"] = [
            "install -d /app/lib/python3/site-packages",
            f"cp -R src/{entry['package']} /app/lib/python3/site-packages/",
        ]
    elif entry["buildsystem"] == "cmake":
        result["buildsystem"] = "cmake-ninja"
        result["config-opts"] = [
            "-DBUILD_SHARED_LIBS=ON",
            "-DCMAKE_INSTALL_LIBDIR=lib",
            "-DBUILD_TESTING=OFF",
        ]
        if name == "curl":
            result["config-opts"] += [
                "-DBUILD_CURL_EXE=OFF",
                "-DCURL_USE_OPENSSL=ON",
                "-DENABLE_THREADED_RESOLVER=ON",
                "-DCURL_DISABLE_WEBSOCKETS=OFF",
                "-DCURL_USE_LIBPSL=OFF",
                "-DCURL_BROTLI=OFF",
                "-DCURL_ZSTD=OFF",
                "-DCURL_USE_LIBSSH2=OFF",
            ]
        else:
            result["config-opts"] += [
                "-DJANSSON_BUILD_SHARED_LIBS=ON",
                "-DJANSSON_BUILD_DOCS=OFF",
                "-DJANSSON_WITHOUT_TESTS=ON",
            ]
    elif entry["buildsystem"] == "autotools":
        result["config-opts"] = ["--enable-shared", "--disable-static", "--libdir=/app/lib"]
        if name == "chafa":
            result["config-opts"] += ["--without-imagemagick", "--without-librsvg"]
        elif name == "ncurses":
            result["config-opts"] = ncurses_options("/app")
    elif name == "ffmpeg":
        result["config-opts"] = [
            "--enable-shared",
            "--disable-static",
            "--disable-programs",
            "--disable-doc",
            "--disable-debug",
            "--disable-gpl",
            "--disable-nonfree",
            "--disable-autodetect",
            "--disable-network",
            "--enable-zlib",
            "--enable-vaapi",
            "--libdir=/app/lib",
        ]
    else:
        result["buildsystem"] = "meson"
        result["config-opts"] = [
            "--libdir=lib",
            "--auto-features=disabled",
            "--default-library=shared",
        ]
        if name == "mpv":
            result["config-opts"] += [
                "-Dgpl=false",
                "-Dlibmpv=true",
                "-Dcplayer=false",
                "-Dgl=disabled",
                "-Dbuild-date=false",
                "-Dpulse=enabled",
                "-Ddrm=enabled",
                "-Dvaapi=enabled",
                "-Dvaapi-drm=enabled",
            ]
        elif name == "libsixel":
            result["config-opts"] += [
                "-Dimg2sixel=disabled",
                "-Dsixel2png=disabled",
                "-Dtests=disabled",
            ]
        elif name == "libsecret":
            result["config-opts"] += [
                "-Dmanpage=false",
                "-Dvapi=false",
                "-Dgtk_doc=false",
                "-Dintrospection=false",
                "-Dcrypto=libgcrypt",
                "-Dtest_setup=disabled",
            ]
        elif name == "libplacebo":
            result["build-options"] = {"env": {"PYTHONPATH": "/app/lib/python3/site-packages"}}
            result["config-opts"] += [
                "-Dvulkan=disabled",
                "-Dopengl=disabled",
                "-Dshaderc=disabled",
                "-Dglslang=disabled",
                "-Ddemos=false",
                "-Dtests=false",
            ]
        elif name == "libass":
            result["config-opts"] += [
                "-Dfontconfig=enabled",
                "-Dlibunibreak=enabled",
                "-Dasm=auto",
            ]
    return result


def manifest(source, digest):
    entries = json.loads((ROOT / "dist/dependencies.json").read_text())["sources"]
    build_dependencies = json.loads((ROOT / "dist/flatpak/build-dependencies.json").read_text())[
        "sources"
    ]
    modules = []
    for entry in entries:
        if "posix" not in entry.get("platforms", ["posix"]):
            continue
        if entry["name"] == "libplacebo":
            modules.extend(module(e) for e in build_dependencies)
        modules.append(module(entry))
    resources = json.loads((ROOT / "dist/resources.json").read_text())["files"]
    fonts = {
        "name": "emoji-font",
        "buildsystem": "simple",
        "build-commands": [
            "install -Dm644 NotoColorEmoji.ttf /app/share/fonts/papo-tui/NotoColorEmoji.ttf",
            "install -Dm644 Noto-OFL.txt /app/share/licenses/papo-tui/Noto-OFL.txt",
        ],
        "sources": [
            {
                "type": "file",
                "url": e["url"],
                "sha256": e["sha256"],
                "dest-filename": "NotoColorEmoji.ttf" if i == 0 else "Noto-OFL.txt",
            }
            for i, e in enumerate(resources)
        ],
    }
    app = {
        "name": "papo-tui",
        "buildsystem": "cmake-ninja",
        "config-opts": [
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_INSTALL_LIBDIR=lib",
            "-DBUILD_TESTING=OFF",
            "-DPAPO_RELEASE_BUILD=ON",
            "-DPAPO_BUNDLE_RUNTIME=OFF",
        ],
        "sources": [{"type": "archive", **source, "sha256": digest}],
    }
    return {
        "app-id": APP_ID,
        "runtime": "org.freedesktop.Platform",
        "runtime-version": RUNTIME,
        "sdk": "org.freedesktop.Sdk",
        "command": "papo-tui",
        "separate-locales": False,
        "finish-args": [
            "--share=network",
            "--device=dri",
            "--socket=pulseaudio",
            "--talk-name=org.freedesktop.Notifications",
            "--talk-name=org.freedesktop.secrets",
            "--talk-name=org.kde.kwalletd5",
            "--talk-name=org.kde.kwalletd6",
        ],
        "cleanup": [
            "/include",
            "/lib/pkgconfig",
            "/lib/*.la",
            "/lib/python3/site-packages/jinja2",
            "/lib/python3/site-packages/markupsafe",
            "/share/man",
        ],
        "modules": modules + [fonts, app],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--url", help="public source URL instead of a local build path")
    args = parser.parse_args()
    source = {"url": args.url} if args.url else {"path": str(args.source.resolve())}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest(source, sha256(args.source)), indent=2) + "\n")


if __name__ == "__main__":
    main()
