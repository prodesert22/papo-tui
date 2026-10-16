#!/usr/bin/env python3
"""Assemble and smoke-test release payloads from a CMake installation."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tarfile
import tempfile
import zipfile

from bootstrap import ROOT, download
from system_sources import collect, collect_licenses

APP_ID = "io.github.prodesert22.PapoTUI"


def run(*args, **kwargs):
    subprocess.run(list(map(str, args)), check=True, **kwargs)


def binary_arch(path):
    with path.open("rb") as source:
        header = source.read(64)
        if header[:4] == b"\x7fELF":
            endian = "<" if header[5] == 1 else ">"
            machine = struct.unpack_from(endian + "H", header, 18)[0]
            return {62: "x86_64", 183: "aarch64"}.get(machine)
        if header[:2] == b"MZ":
            source.seek(struct.unpack_from("<I", header, 60)[0])
            signature = source.read(6)
            if signature[:4] == b"PE\0\0":
                return {0x8664: "x86_64", 0xAA64: "aarch64"}.get(
                    struct.unpack_from("<H", signature, 4)[0]
                )
    return None


def cmake_cache(path):
    values = {}
    for line in path.read_text().splitlines():
        if (
            not line
            or line.startswith(("#", "//"))
            or "=" not in line
            or ":" not in line.split("=", 1)[0]
        ):
            continue
        key, value = line.split("=", 1)
        values[key.split(":", 1)[0]] = value
    return values


def install(build, destination):
    # DESTDIR inherited from another packaging tool must not redirect this install.
    env = dict(os.environ)
    env.pop("DESTDIR", None)
    run("cmake", "--install", build, "--prefix", destination, env=env)


def payload_binary(stage):
    candidates = [stage / "bin/papo-tui", stage / "papo-tui.exe", stage / "bin/papo-tui.exe"]
    matches = [p for p in candidates if p.is_file()]
    if len(matches) != 1:
        raise RuntimeError("Installation must contain exactly one application executable")
    return matches[0]


def smoke(stage, version, architecture):
    binary = payload_binary(stage)
    if os.name == "nt":
        if (
            not (stage / "share/terminfo").is_dir()
            or not (stage / "etc/fonts/fonts.conf").is_file()
        ):
            raise RuntimeError("Windows runtime data is incomplete")
    if binary_arch(binary) != architecture:
        raise RuntimeError(f"Wrong machine type in {binary.name}")
    env = dict(os.environ)
    env["PATH"] = str(binary.parent) + os.pathsep + env.get("PATH", "")
    env["LD_LIBRARY_PATH"] = os.pathsep.join(
        [str(stage / "lib/papo-tui"), str(stage / "lib64/papo-tui")]
    )
    with tempfile.TemporaryDirectory(prefix="papo smoke á ") as cwd:
        result = subprocess.check_output(
            [str(binary), "--version"], cwd=cwd, env=env, text=True, timeout=15
        )
        if result.strip() != "papo-tui " + version:
            raise RuntimeError("Installed binary version mismatch")
        subprocess.run(
            [str(binary), "--help"],
            cwd=cwd,
            env=env,
            check=True,
            timeout=15,
            stdout=subprocess.DEVNULL,
        )
    modules = list((binary.parent / "papo-backends").glob("*"))
    if len([p for p in modules if p.suffix in (".dll", ".so")]) != 2:
        raise RuntimeError("Installation is missing image/video modules")
    for module in modules:
        if module.suffix in (".dll", ".so") and binary_arch(module) != architecture:
            raise RuntimeError("Backend architecture mismatch")
    worker = (
        binary.parent
        / "papo-backends"
        / ("papo-media-worker.exe" if binary.suffix == ".exe" else "papo-media-worker")
    )
    if not worker.is_file():
        raise RuntimeError("Installation is missing the media helper")
    if binary_arch(worker) != architecture:
        raise RuntimeError("Media helper architecture mismatch")


def source_archive(output, version):
    target = output / f"papo-tui-{version}-source.tar.gz"
    # Explicit allowlist excludes credentials, build trees, worktrees and internal reports.
    roots = ["src", "cmake", "dist", "data", "docs", "tests", "tools", ".github"]
    files = [
        p
        for name in roots
        for p in (ROOT / name).rglob("*")
        if p.is_file() and "__pycache__" not in p.parts and p.suffix not in (".pyc", ".pyo")
    ]
    files += [
        p
        for p in ROOT.iterdir()
        if p.is_file()
        and p.name
        in (
            "CMakeLists.txt",
            "CMakePresets.json",
            "README.md",
            "LICENSE",
            "THIRD_PARTY_NOTICES.md",
            "CHANGELOG.md",
            "CONTRIBUTING.md",
            "AGENTS.md",
            "requirements-dev.txt",
            "openapi.yml",
            ".clang-format",
            ".clang-format-ignore",
            ".cmake-format.yaml",
            "pyproject.toml",
            ".gitignore",
        )
    ]
    files += list((ROOT / "releases").glob("*.md"))
    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
    import gzip

    with (
        target.open("wb") as raw,
        gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=epoch) as compressed,
    ):
        if os.environ.get("GITHUB_ACTIONS") == "true":
            # Git blobs and executable bits are identical on Windows and Linux.
            # Platform checkout newline conversion must not change source assets.
            paths = sorted({p.relative_to(ROOT).as_posix() for p in files})
            completed = subprocess.run(
                [
                    "git",
                    "archive",
                    "--format=tar",
                    "--prefix=" + f"papo-tui-{version}/",
                    "HEAD",
                    "--",
                    *paths,
                ],
                cwd=ROOT,
                stdout=subprocess.PIPE,
                check=True,
            )
            compressed.write(completed.stdout)
            return target
        with tarfile.open(fileobj=compressed, mode="w") as archive:
            for path in sorted(set(files)):
                info = archive.gettarinfo(
                    str(path), arcname=f"papo-tui-{version}/" + path.relative_to(ROOT).as_posix()
                )
                info.uid = info.gid = 0
                info.uname = info.gname = ""
                info.mtime = epoch
                with path.open("rb") as stream:
                    archive.addfile(info, stream)
    return target


def dependency_sources(output, version, architecture, flavor, prefix, cache, stage):
    if not prefix:
        raise RuntimeError("Release packaging requires --dependency-prefix for source provenance")
    lock = json.loads((ROOT / "dist/dependencies.json").read_text())
    with tempfile.TemporaryDirectory(dir=output, prefix="source-kit-") as tmp:
        kit = Path(tmp)
        for entry in lock["sources"]:
            if os.name not in entry.get("platforms", [os.name]):
                continue
            archive = cache / entry["archive"]
            download(entry["url"], entry["sha256"], archive)
            shutil.copy2(archive, kit / archive.name)
            stamp = prefix / (".papo-" + entry["name"] + ".json")
            if not stamp.exists():
                raise RuntimeError(f"Dependency provenance missing: {entry['name']}")
            shutil.copy2(stamp, kit / stamp.name)
        (kit / "dist").mkdir()
        shutil.copy2(ROOT / "dist/bootstrap.py", kit / "dist/bootstrap.py")
        shutil.copy2(ROOT / "dist/dependencies.json", kit / "dist/dependencies.json")
        shutil.copy2(ROOT / "dist/resources.json", kit / "dist/resources.json")
        shutil.copy2(ROOT / "dist/resources.py", kit / "dist/resources.py")
        resources = ROOT / "build/resources"
        if resources.is_dir():
            shutil.copytree(resources, kit / "font-resources")
        collect(stage, prefix, kit / "system-sources")
        licenses = prefix / "share/licenses/papo-dependencies"
        if licenses.is_dir():
            shutil.copytree(licenses, kit / "licenses")
        inventory = kit / "system-packages.txt"
        if os.name == "nt" and shutil.which("pacman"):
            inventory.write_text(subprocess.check_output(["pacman", "-Q"], text=True))
        elif shutil.which("dpkg-query"):
            inventory.write_text(subprocess.check_output(["dpkg-query", "-W"], text=True))
        elif shutil.which("rpm"):
            inventory.write_text(subprocess.check_output(["rpm", "-qa"], text=True))
        target = output / f"papo-tui-{version}-{flavor}-{architecture}-dependency-sources.tar.gz"
        with tarfile.open(target, "w:gz") as archive:
            archive.add(kit, arcname="dependency-sources")
    return target


def appimage(stage, output, version, architecture, cache):
    appdir = stage.parent / "PapoTUI.AppDir"
    (appdir / "usr").mkdir(parents=True)
    shutil.copytree(stage, appdir / "usr", dirs_exist_ok=True, symlinks=True)
    for name in (APP_ID + ".desktop", APP_ID + ".svg"):
        shutil.copy2(ROOT / "dist" / name, appdir / name)
    shutil.copy2(ROOT / "dist/appimage/AppRun", appdir / "AppRun")
    (appdir / "AppRun").chmod(0o755)
    toolname = f"appimagetool-{architecture}.AppImage"
    tools = json.loads((ROOT / "dist/tools.json").read_text())["appimagetool"]["assets"]
    entry = next(x for x in tools if x["name"] == toolname)
    tool = download(entry["url"], entry["sha256"], cache / toolname)
    tool.chmod(0o755)
    result = output / f"PapoTUI-{version}-linux-{architecture}.AppImage"
    result.unlink(missing_ok=True)
    env = dict(os.environ, ARCH=architecture, APPIMAGE_EXTRACT_AND_RUN="1", NO_APPSTREAM="1")
    run(tool, appdir, result, env=env)
    # Exercise the portable entrypoint from an unrelated directory without FUSE.
    with tempfile.TemporaryDirectory(prefix="papo-appimage-") as cwd:
        run(result, "--version", cwd=cwd, env=dict(os.environ, APPIMAGE_EXTRACT_AND_RUN="1"))
    return result


def arch_package(stage, output, version, architecture):
    work = stage.parent / "arch"
    work.mkdir()
    payload = work / "payload.tar.gz"
    with tarfile.open(payload, "w:gz") as archive:
        archive.add(stage, arcname="usr")
    digest = hashlib.sha256(payload.read_bytes()).hexdigest()
    recipe = (ROOT / "dist/arch/PKGBUILD.in").read_text()
    for key, value in {"VERSION": version, "ARCH": architecture, "SHA256": digest}.items():
        recipe = recipe.replace("@" + key + "@", value)
    (work / "PKGBUILD").write_text(recipe)
    configuration = work / "makepkg.conf"
    configuration.write_text(
        "source /etc/makepkg.conf\nPKGEXT='.pkg.tar.zst'\n"
        f"CARCH='{architecture}'\nCOMPRESSZST=(zstd -c -T2 -10 -)\n"
    )
    env = dict(os.environ, CARCH=architecture, PKGDEST=str(output))
    run(
        "makepkg",
        "--config",
        configuration,
        "--force",
        "--nodeps",
        "--noconfirm",
        cwd=work,
        env=env,
    )
    candidates = list(output.glob(f"papo-tui-{version}-1-{architecture}.pkg.tar.zst"))
    if len(candidates) != 1 or candidates[0].suffix != ".zst":
        raise RuntimeError("Expected one zstd Arch package")
    candidates[0].rename(output / f"papo-tui-{version}-arch-{architecture}.pkg.tar.zst")


def installer(stage, output, version, architecture):
    compiler = os.environ.get("ISCC") or shutil.which("ISCC.exe") or shutil.which("iscc")
    if not compiler:
        raise RuntimeError("Set ISCC to the verified Inno Setup compiler")
    script = stage.parent / "papo-tui.iss"
    recipe = (ROOT / "dist/windows/setup.iss.in").read_text()
    values = {
        "VERSION": version,
        "ARCH": architecture,
        "SOURCE": str(stage),
        "OUTPUT": str(output),
        "ARCH_ALLOWED": "arm64" if architecture == "aarch64" else "x64compatible and not arm64",
    }
    for key, value in values.items():
        recipe = recipe.replace("@" + key + "@", value)
    script.write_text(recipe)
    run(compiler, script)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--formats", required=True, help="deb,rpm,arch,appimage,zip,installer")
    parser.add_argument("--arch", choices=["x86_64", "aarch64"], required=True)
    parser.add_argument("--flavor", default="linux")
    parser.add_argument("--dependency-prefix", type=Path, required=True)
    parser.add_argument("--cache", type=Path, default=ROOT / "build/source-cache")
    args = parser.parse_args()
    build = args.build.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cache = args.cache.resolve()
    cache.mkdir(parents=True, exist_ok=True)
    values = cmake_cache(build / "CMakeCache.txt")
    if values.get("PAPO_RELEASE_BUILD") != "ON":
        parser.error("Configure with PAPO_RELEASE_BUILD=ON")
    version = values["CMAKE_PROJECT_VERSION"]
    selected = set(args.formats.split(","))
    if selected - {"deb", "rpm", "arch", "appimage", "zip", "installer"}:
        parser.error("Unknown package format")
    with tempfile.TemporaryDirectory(prefix="packaging-", dir=build) as temporary:
        work = Path(temporary)
        stage = work / "stage"
        install(build, stage)
        smoke(stage, version, args.arch)
        collect_licenses(stage, args.dependency_prefix.resolve())
        for fmt in sorted(selected):
            if fmt in ("deb", "rpm"):
                destination = work / fmt
                run(
                    "cpack",
                    "--config",
                    build / "CPackConfig.cmake",
                    "-G",
                    fmt.upper(),
                    "-B",
                    destination,
                    "-D",
                    "CPACK_INSTALL_CMAKE_PROJECTS=",
                    "-D",
                    "CPACK_INSTALLED_DIRECTORIES=" + str(stage) + ";/usr",
                )
                candidates = list(destination.glob("*." + fmt))
                if len(candidates) != 1:
                    raise RuntimeError("Expected one native package")
                shutil.copy2(
                    candidates[0], output / f"papo-tui-{version}-{args.flavor}-{args.arch}.{fmt}"
                )
            elif fmt == "appimage":
                appimage(stage, output, version, args.arch, cache)
            elif fmt == "arch":
                arch_package(stage, output, version, args.arch)
            elif fmt == "installer":
                installer(stage, output, version, args.arch)
            else:
                target = output / f"papo-tui-{version}-windows-{args.arch}.zip"
                with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
                    for path in sorted(stage.rglob("*")):
                        if path.is_file():
                            archive.write(path, path.relative_to(stage))
        source_archive(output, version)
        dependency_sources(
            output,
            version,
            args.arch,
            args.flavor,
            args.dependency_prefix.resolve() if args.dependency_prefix else None,
            cache,
            stage,
        )


if __name__ == "__main__":
    main()
