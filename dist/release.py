#!/usr/bin/env python3
"""Validate release identity and assembled assets before publishing."""

import argparse
from pathlib import Path
import re
import subprocess
from bootstrap import sha256

ROOT = Path(__file__).resolve().parents[1]
SEMVER = re.compile(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\Z")


def git(*arguments):
    return subprocess.check_output(["git", *arguments], cwd=ROOT, text=True).strip()


def project_version(ref=None):
    text = git("show", f"{ref}:CMakeLists.txt") if ref else (ROOT / "CMakeLists.txt").read_text()
    match = re.search(r"project\(\s*papo_tui\s+VERSION\s+([^\s)]+)", text)
    if not match or not SEMVER.fullmatch("v" + match[1]):
        raise ValueError("CMake must declare a stable SemVer version")
    return match[1]


def validate_tag(tag):
    if not SEMVER.fullmatch(tag):
        raise ValueError("Use a stable SemVer tag: vMAJOR.MINOR.PATCH")
    version = tag[1:]
    if project_version() != version:
        raise ValueError("Tag differs from the CMake version")
    if f"## [{version}]" not in (ROOT / "CHANGELOG.md").read_text():
        raise ValueError("Missing changelog entry")
    if not (ROOT / "releases" / f"{tag}.md").is_file():
        raise ValueError("Missing full release notes")
    return version


def version_change(base):
    previous, current = project_version(base), project_version()
    if previous == current:
        return None
    if tuple(map(int, current.split("."))) <= tuple(map(int, previous.split("."))):
        raise ValueError("The release version must increase")
    return validate_tag("v" + current)


def require_main():
    subprocess.run(
        ["git", "merge-base", "--is-ancestor", "HEAD", "refs/remotes/origin/main"],
        cwd=ROOT,
        check=True,
    )


def ensure_tag(tag):
    validate_tag(tag)
    require_main()
    subprocess.run(
        [
            "git",
            "diff",
            "--quiet",
            "HEAD",
            "--",
            "CMakeLists.txt",
            "CHANGELOG.md",
            f"releases/{tag}.md",
        ],
        cwd=ROOT,
        check=True,
    )
    head = git("rev-parse", "HEAD")
    ref = "refs/tags/" + tag
    exists = subprocess.run(["git", "show-ref", "--verify", "--quiet", ref], cwd=ROOT, check=False)
    if exists.returncode == 1:
        subprocess.run(
            [
                "git",
                "-c",
                "user.name=github-actions[bot]",
                "-c",
                "user.email=41898282+github-actions[bot]@users.noreply.github.com",
                "-c",
                "tag.gpgSign=false",
                "tag",
                "--annotate",
                tag,
                head,
                "--message",
                tag,
            ],
            cwd=ROOT,
            check=True,
        )
    elif exists.returncode != 0:
        exists.check_returncode()
    if git("rev-parse", ref + "^{commit}") != head:
        raise ValueError("Release tag already points to a different commit")
    # An explicit tag ref cannot update main. A conflicting remote tag is rejected.
    subprocess.run(["git", "push", "origin", ref + ":" + ref], cwd=ROOT, check=True)


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
    parser.add_argument("tag", nargs="?")
    parser.add_argument("--assets", type=Path)
    parser.add_argument("--require-head", action="store_true")
    parser.add_argument("--require-main", action="store_true")
    parser.add_argument("--ensure-tag", action="store_true")
    parser.add_argument("--changed-since", help="print the new version, or nothing if unchanged")
    args = parser.parse_args()
    if args.changed_since:
        if args.tag or args.assets or args.require_head or args.require_main or args.ensure_tag:
            parser.error("--changed-since cannot be combined with tag operations")
        version = version_change(args.changed_since)
        if version:
            print(version)
        return
    if not args.tag:
        parser.error("Provide a tag or --changed-since")
    version = validate_tag(args.tag)
    if args.require_main:
        require_main()
    if args.ensure_tag:
        ensure_tag(args.tag)
    if args.require_head:
        head = git("rev-parse", "HEAD")
        target = git("rev-parse", "refs/tags/" + args.tag + "^{commit}")
        if head != target:
            raise ValueError("Checkout differs from the requested tag")
    if args.assets:
        check_assets(args.assets, version)
    print(version)


if __name__ == "__main__":
    main()
