#!/usr/bin/env python3
"""Make a test disk image of a macOS build: Papo TUI.app with its libraries, ad hoc signed.

This is a build to try the program on a Mac, not a release package: nothing publishes it, it
carries no update channel and it is not notarized, so macOS asks before opening it once.

The tree that `cmake --install` writes keeps its Linux layout inside the bundle, under
Contents/Resources/papo-tui, so the program finds its media helper, modules, voice plugins and data
exactly as an unpacked Linux archive does. Every library outside macOS itself (the private prefix
and Homebrew) is copied to lib/papo-tui and every reference to it is rewritten relative to the file
that holds it, so the copy runs on a Mac without Homebrew. Double-clicking the app opens the
program in the default terminal.
"""

import argparse
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
APP_ID = "io.github.prodesert22.PapoTUI"
APP_NAME = "Papo TUI"
TREE = Path("Contents/Resources/papo-tui")
LIBRARIES = Path("lib/papo-tui")
# The dyld shared cache serves these; they have no file to copy.
SYSTEM = ("/usr/lib/", "/System/")
# 64-bit Mach-O (either byte order) and universal binaries.
MACHO = {b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca"}
# The same plugins as the Linux bundle (cmake/Packaging.cmake), with Core Audio for devices.
VOICE_PLUGINS = (
    "coreelements",
    "audioconvert",
    "audioresample",
    "audiomixer",
    "volume",
    "opus",
    "level",
    "rtp",
    "rtpmanager",
    "dtls",
    "srtp",
    "sctp",
    "webrtc",
    "nice",
    "autodetect",
    "webrtcdsp",
    "osxaudio",
)
LAUNCHER = """#!/bin/sh
# Finder starts an app without a terminal, which a terminal program needs. Open the program in the
# terminal that macOS uses for command-line programs (Terminal unless another one was chosen).
program="$(cd "$(dirname "$0")/../Resources/papo-tui/bin" && pwd -P)/papo-tui"
exec /usr/bin/open "$program" || exec /usr/bin/open -a Terminal "$program"
"""
README = """Papo TUI for macOS (Apple Silicon) - test build
=================================================

1. Drag "Papo TUI" to Applications.
2. This build is not notarized by Apple. Before the first start, run this once in Terminal:

       xattr -dr com.apple.quarantine "/Applications/Papo TUI.app"

   (Or open it, then allow it in System Settings > Privacy & Security > "Open Anyway".)
3. Double-click "Papo TUI": it opens in your terminal. To start it from a terminal instead:

       "/Applications/Papo TUI.app/Contents/Resources/papo-tui/bin/papo-tui"

Copying uses the macOS clipboard, and Ctrl+V pastes files copied in Finder, images and text. Not
available on macOS yet: voice calls, desktop notifications, the system file picker and saved
sessions. Images use terminal graphics in kitty,
Ghostty, WezTerm and iTerm2, and colored characters in Terminal.

---

Papo TUI para macOS (Apple Silicon) - build de teste: arraste "Papo TUI" para Aplicativos e, antes
de abrir pela primeira vez, rode no Terminal:
    xattr -dr com.apple.quarantine "/Applications/Papo TUI.app"
"""


def run(*args, **kwargs):
    print("+", *map(str, args), flush=True)
    subprocess.run(list(map(str, args)), check=True, **kwargs)


def output(*args):
    return subprocess.run(list(map(str, args)), check=True, capture_output=True, text=True).stdout


def is_macho(path):
    if path.is_symlink() or not path.is_file():
        return False
    with path.open("rb") as stream:
        return stream.read(4) in MACHO


def install_names(path):
    """What path loads, as written in it (otool -L), without the library's own name."""
    own = output("otool", "-D", path).splitlines()[1:]
    own = own[0].strip() if own else None
    names = []
    for line in output("otool", "-L", path).splitlines()[1:]:
        line = line.strip()
        if not line or line.endswith(":"):
            continue
        name = line.split(" (compatibility version")[0]
        if name != own and name not in names:
            names.append(name)
    return names


def rpaths(path):
    text = output("otool", "-l", path)
    return re.findall(r"cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (.+?) \(offset \d+\)", text)


def resolve(name, origin, search):
    """The file that name refers to, seen from origin, the file's own (unbundled) location."""
    here = origin.parent
    if name.startswith("@loader_path/") or name.startswith("@executable_path/"):
        candidates = [here / name.split("/", 1)[1]]
    elif name.startswith("@rpath/"):
        rest = name.split("/", 1)[1]
        candidates = [
            Path(entry.replace("@loader_path", str(here)).replace("@executable_path", str(here)))
            / rest
            for entry in rpaths(origin)
        ]
        candidates += [directory / rest for directory in search]
    else:
        candidates = [Path(name)]
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise SystemExit(f"{origin}: cannot find {name}")


def bundle_libraries(tree, search):
    """Copies every non-system library that the tree needs, then points each reference at it."""
    libraries = tree / LIBRARIES
    libraries.mkdir(parents=True, exist_ok=True)
    copies = {}  # resolved source -> its copy in the bundle
    references = {}  # file in the bundle -> [(name written in it, file it means)]
    pending = [(path, path) for path in sorted(tree.rglob("*")) if is_macho(path)]
    while pending:
        file, origin = pending.pop()
        for name in install_names(origin):
            if name.startswith(SYSTEM):
                continue
            source = resolve(name, origin, search)
            if source.is_relative_to(tree.resolve()):
                target = source
            elif source in copies:
                target = copies[source]
            else:
                target = libraries / Path(name).name
                if target.exists():
                    raise SystemExit(f"Two libraries named {target.name}: {source}")
                shutil.copyfile(source, target)
                target.chmod(0o755)
                copies[source] = target
                pending.append((target, source))
            references.setdefault(file, []).append((name, target))
    for file in sorted({*references, *copies.values()}):
        arguments = []
        if file.parent == libraries:
            arguments += ["-id", "@rpath/" + file.name]
        for name, target in references.get(file, []):
            relative = os.path.relpath(target.resolve(), file.resolve().parent)
            new = "@loader_path/" + relative
            if new != name:
                arguments += ["-change", name, new]
        if arguments:
            run("install_name_tool", *arguments, file)
    return copies


def check_references(app):
    """Fails when any Mach-O of the app still names a library outside the app or macOS."""
    for path in sorted(app.rglob("*")):
        if not is_macho(path):
            continue
        for name in install_names(path):
            if not name.startswith(SYSTEM) and not name.startswith("@loader_path/"):
                raise SystemExit(f"{path} still loads {name}")
            if name.startswith("@loader_path/"):
                target = path.parent / name.split("/", 1)[1]
                if not target.is_file():
                    raise SystemExit(f"{path} loads {name}, which is missing from the app")


def icon(resources):
    """PapoTUI.icns from the project's SVG, with the tools of librsvg and macOS."""
    svg = ROOT / "dist" / (APP_ID + ".svg")
    if not shutil.which("rsvg-convert") or not shutil.which("iconutil"):
        print("Skipping the app icon: rsvg-convert or iconutil is missing", flush=True)
        return None
    with tempfile.TemporaryDirectory() as directory:
        iconset = Path(directory) / "PapoTUI.iconset"
        iconset.mkdir()
        for size in (16, 32, 128, 256, 512):
            for scale in (1, 2):
                suffix = "" if scale == 1 else "@2x"
                run(
                    "rsvg-convert",
                    "-w",
                    size * scale,
                    "-h",
                    size * scale,
                    "-o",
                    iconset / f"icon_{size}x{size}{suffix}.png",
                    svg,
                )
        run("iconutil", "-c", "icns", "-o", resources / "PapoTUI.icns", iconset)
    return "PapoTUI"


def version(build):
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    match = re.search(r"^PAPO_PACKAGE_VERSION:INTERNAL=(.+)$", cache, re.MULTILINE)
    if not match:
        raise SystemExit(f"{build} is not a configured Papo TUI build")
    return match.group(1).strip()


def disk_image(staging, target):
    # hdiutil sometimes finds the new volume busy on CI machines; a later attempt succeeds.
    for attempt in range(3):
        try:
            run(
                "hdiutil",
                "create",
                "-volname",
                APP_NAME,
                "-srcfolder",
                staging,
                "-fs",
                "HFS+",
                "-format",
                "UDZO",
                "-ov",
                target,
            )
            return
        except subprocess.CalledProcessError:
            if attempt == 2:
                raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True, help="configured and built CMake tree")
    parser.add_argument("--prefix", type=Path, required=True, help="private dependency prefix")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    build, prefix = args.build.resolve(), args.prefix.resolve()
    full_version = version(build)
    homebrew = Path(output("brew", "--prefix").strip())
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=args.output, prefix="macos-app-") as directory:
        staging = Path(directory)
        app = staging / (APP_NAME + ".app")
        tree = app / TREE
        run("cmake", "--install", build, "--prefix", tree)
        plugins = tree / "bin/papo-backends/gstreamer-1.0"
        plugins.mkdir(parents=True, exist_ok=True)
        for plugin in VOICE_PLUGINS:
            found = sorted((prefix / "lib/gstreamer-1.0").glob(f"libgst{plugin}.*"))
            found = [path for path in found if path.suffix in (".dylib", ".so")]
            if found:
                shutil.copyfile(found[0], plugins / found[0].name)
            else:
                print(f"Voice plugin {plugin} is missing; voice stays unavailable", flush=True)
        # Homebrew's font configuration, links resolved: it lists the macOS font folders.
        shutil.copytree(
            homebrew / "etc/fonts",
            tree / "etc/fonts",
            symlinks=False,
            ignore_dangling_symlinks=True,
        )
        copies = bundle_libraries(tree, [prefix / "lib", homebrew / "lib"])
        print(f"Copied {len(copies)} libraries into the app", flush=True)
        macos = app / "Contents/MacOS"
        macos.mkdir(parents=True)
        launcher = macos / "papo-tui-launcher"
        launcher.write_text(LAUNCHER, encoding="utf-8")
        launcher.chmod(0o755)
        resources = app / "Contents/Resources"
        icon_name = icon(resources)
        numeric = full_version.split("-", 1)[0]
        info = {
            "CFBundleDevelopmentRegion": "en",
            "CFBundleDisplayName": APP_NAME,
            "CFBundleExecutable": launcher.name,
            "CFBundleIdentifier": APP_ID,
            "CFBundleInfoDictionaryVersion": "6.0",
            "CFBundleName": APP_NAME,
            "CFBundlePackageType": "APPL",
            "CFBundleShortVersionString": numeric,
            "CFBundleVersion": numeric,
            "LSApplicationCategoryType": "public.app-category.social-networking",
            "LSMinimumSystemVersion": output("sw_vers", "-productVersion").strip().split(".")[0]
            + ".0",
            "NSHighResolutionCapable": True,
        }
        if icon_name:
            info["CFBundleIconFile"] = icon_name
        with (app / "Contents/Info.plist").open("wb") as stream:
            plistlib.dump(info, stream)
        check_references(app)
        # Rewriting a Mach-O voids its signature, and Apple Silicon refuses unsigned code: sign
        # every file again (ad hoc), then the app around them.
        for path in sorted(app.rglob("*")):
            if is_macho(path):
                run("codesign", "--force", "--sign", "-", path)
        try:
            run("codesign", "--force", "--sign", "-", app)
        except subprocess.CalledProcessError:
            print("The app itself stays unsigned; each program and library is signed", flush=True)
        run(tree / "bin/papo-tui", "--version")
        (staging / "Applications").symlink_to("/Applications")
        (staging / "READ ME FIRST.txt").write_text(README, encoding="utf-8")
        target = args.output / f"papo-tui-{full_version}-macos-aarch64-test.dmg"
        disk_image(staging, target)
    print(f"Wrote {target}", flush=True)


if __name__ == "__main__":
    main()
