#!/usr/bin/env python3
"""Validate release identity and assembled assets before publishing."""

import argparse
from pathlib import Path
import re
import subprocess
from bootstrap import sha256

ROOT = Path(__file__).resolve().parents[1]
SEMVER = re.compile(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\Z")


def validate_tag(tag):
    if not SEMVER.fullmatch(tag):
        raise ValueError("Use a stable SemVer tag: vMAJOR.MINOR.PATCH")
    version = tag[1:]
    text = (ROOT / "CMakeLists.txt").read_text()
    if not re.search(r"project\(\s*papo_tui\s+VERSION\s+" + re.escape(version) + r"\s", text):
        raise ValueError("Tag differs from the CMake version")
    if f"## [{version}]" not in (ROOT / "CHANGELOG.md").read_text():
        raise ValueError("Missing changelog entry")
    if not (ROOT / "releases" / f"{tag}.md").is_file():
        raise ValueError("Missing full release notes")
    return version


def assets(version):
    names = {f"papo-tui-{version}-source.tar.gz"}
    for arch in ("x86_64", "aarch64"):
        for flavor, extension in (("ubuntu", "deb"), ("debian", "deb"), ("fedora", "rpm")):
            names.add(f"papo-tui-{version}-{flavor}-{arch}.{extension}")
            names.add(f"papo-tui-{version}-{flavor}-{arch}-dependency-sources.tar.gz")
        names.update(
            (
                f"papo-tui-{version}-arch-{arch}.pkg.tar.zst",
                f"PapoTUI-{version}-linux-{arch}.AppImage",
                f"papo-tui-{version}-flatpak-{arch}.flatpak",
                f"papo-tui-{version}-flatpak-{arch}-sources.tar.gz",
                f"papo-tui-{version}-windows-{arch}.zip",
                f"papo-tui-{version}-windows-{arch}-setup.exe",
                f"papo-tui-{version}-windows-{arch}-dependency-sources.tar.gz",
            )
        )
    return names


def check_assets(directory, version):
    expected = assets(version)
    actual = {p.name for p in directory.iterdir() if p.is_file()}
    if expected != actual:
        raise ValueError(
            f"Assets differ: missing={sorted(expected - actual)}, extra={sorted(actual - expected)}"
        )
    lines = []
    for name in sorted(expected):
        path = directory / name
        if path.stat().st_size == 0:
            raise ValueError(f"Empty artifact: {name}")
        digest = sha256(path)
        lines.append(f"{digest}  {name}\n")
    (directory / "SHA256SUMS").write_text("".join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tag")
    parser.add_argument("--assets", type=Path)
    parser.add_argument("--require-head", action="store_true")
    args = parser.parse_args()
    version = validate_tag(args.tag)
    if args.require_head:
        head = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
        target = subprocess.check_output(
            ["git", "rev-parse", args.tag + "^{commit}"], text=True
        ).strip()
        if head != target:
            raise ValueError("Checkout differs from the requested tag")
    if args.assets:
        check_assets(args.assets, version)
    print(version)


if __name__ == "__main__":
    main()
