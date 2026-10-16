#!/usr/bin/env python3
"""Build locked dependencies in a private prefix; never overwrite system libraries."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


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


def build(entry, source, work, prefix, jobs, env):
    def run(*args, cwd=source):
        print("+", *map(str, args), flush=True)
        subprocess.run(list(map(str, args)), cwd=cwd, env=env, check=True)

    name = entry["name"]
    binary = work / "build"
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
        elif name == "jansson":
            options += [
                "-DJANSSON_BUILD_SHARED_LIBS=ON",
                "-DJANSSON_BUILD_DOCS=OFF",
                "-DJANSSON_WITHOUT_TESTS=ON",
            ]
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
        run("sh", unix_path(source / "configure"), *options, cwd=binary)
        run("make", "-j", jobs, cwd=binary)
        run("make", "install", cwd=binary)
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
            "--disable-network",
            "--enable-zlib",
        ]
        if os.name == "nt":
            options += ["--target-os=mingw32", "--cc=clang", "--cxx=clang++"]
            if os.environ.get("MSYSTEM") == "CLANGARM64":
                options += ["--arch=aarch64"]
            else:
                options += ["--arch=x86_64"]
        run("sh", unix_path(source / "configure"), *options, cwd=binary)
        run("make", "-j", jobs, cwd=binary)
        run("make", "install", cwd=binary)
    else:
        options = [
            "--prefix=" + str(prefix),
            "--libdir=lib",
            "--buildtype=release",
            "--default-library=shared",
            "--auto-features=disabled",
        ]
        if name == "mpv":
            options += [
                "-Dgpl=false",
                "-Dlibmpv=true",
                "-Dcplayer=false",
                "-Dgl=disabled",
                "-Dbuild-date=false",
            ]
            options += (
                ["-Dwasapi=enabled"] if os.name == "nt" else ["-Dpulse=enabled", "-Dalsa=enabled"]
            )
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
            env.get("PKG_CONFIG_PATH", ""),
        ]
    )
    env["PATH"] = str(prefix / "bin") + os.pathsep + env["PATH"]
    if os.name != "nt":
        env["LDFLAGS"] = (
            "-L"
            + str(prefix / "lib")
            + " -Wl,-rpath,"
            + str(prefix / "lib")
            + " "
            + env.get("LDFLAGS", "")
        )
    selected = set(args.only.split(",")) if args.only else {x["name"] for x in lock["sources"]}
    unknown = selected - {x["name"] for x in lock["sources"]}
    if unknown:
        parser.error(f"Unknown sources: {sorted(unknown)}")
    for entry in lock["sources"]:
        if entry["name"] not in selected:
            continue
        if os.name not in entry.get("platforms", [os.name]):
            continue
        stamp = prefix / (".papo-" + entry["name"] + ".json")
        identity = {
            "source": entry,
            "system": os.name,
            "architecture": os.environ.get(
                "MSYSTEM", os.uname().machine if hasattr(os, "uname") else "windows"
            ),
            "prefix": str(prefix),
        }
        if stamp.exists() and json.loads(stamp.read_text()) == identity:
            continue
        archive = download(entry["url"], entry["sha256"], cache / entry["archive"])
        work = prefix.parent / ("dependency-" + entry["name"])
        if work.exists():
            shutil.rmtree(work)
        source = unpack(archive, work / "source")
        build(entry, source, work, prefix, args.jobs, env)
        # Keep exact sources, build commands/options and upstream licenses available to packaging.
        licenses = prefix / "share/licenses/papo-dependencies" / entry["name"]
        licenses.mkdir(parents=True, exist_ok=True)
        for candidate in source.iterdir():
            if candidate.is_file() and candidate.name.upper().startswith(
                ("COPYING", "LICENSE", "COPYRIGHT", "AUTHORS")
            ):
                shutil.copy2(candidate, licenses / candidate.name)
        stamp.write_text(json.dumps(identity, indent=2) + "\n")
    print(prefix)


if __name__ == "__main__":
    main()
