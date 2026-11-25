#!/usr/bin/env python3
"""Build the Windows CI toolchain under C:\\papo-ci, the Windows counterpart of dist/ci/Dockerfile.

The tree holds MSYS2 with the packages of dist/windows/toolchain.json, CPython with the hashed
Python lock and every locked library, already compiled. tools/ci.py packs it once per hash of
its inputs; GitHub and the Windows VM then unpack those same bytes. MSYS2 has no dated package
snapshots, so a new "epoch" in toolchain.json is how a newer set of packages is taken.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path, PureWindowsPath
import shutil
import stat
import subprocess
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
TREE = Path("C:/papo-ci")
LOCK = json.loads((ROOT / "dist/windows/toolchain.json").read_text(encoding="utf-8"))


def system(arch):
    return LOCK["systems"][arch]


def packages(arch):
    return [name.format(package=system(arch)["package"]) for name in LOCK["packages"]]


def fetch(url, digest, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest() == digest:
        return path
    part = path.with_name(path.name + ".part")
    with urllib.request.urlopen(url, timeout=300) as response, part.open("wb") as output:
        shutil.copyfileobj(response, output)
    if hashlib.sha256(part.read_bytes()).hexdigest() != digest:
        part.unlink()
        raise RuntimeError(f"Checksum mismatch: {url}")
    part.replace(path)
    return path


def shell_environment(arch, tree=TREE):
    """The environment of an MSYS2 login shell for this architecture's toolchain."""
    env = dict(os.environ)
    env.update(MSYSTEM=system(arch)["msystem"], CHERE_INVOKING="1", MSYS2_PATH_TYPE="minimal")
    env["PAPO_SYSTEM_BIN"] = str(PureWindowsPath(tree) / "msys64" / system(arch)["prefix"] / "bin")
    return env


def bash(arch, script, cwd, tree=TREE, env=None, check=True):
    """Run a script in the MSYS2 login shell, with the toolchain's Python first on PATH."""
    venv = "/" + str(PureWindowsPath(tree)).replace(":", "").replace("\\", "/").lower() + "/venv"
    command = [
        str(tree / "msys64/usr/bin/bash.exe"),
        "-lc",
        f'export PATH="{venv}/Scripts:$PATH"; cd "$(cygpath -u "$PAPO_CWD")" && {script}',
    ]
    environment = dict(env or shell_environment(arch, tree), PAPO_CWD=str(cwd))
    return subprocess.run(command, env=environment, check=check)


def remove(tree):
    """Delete a tree MSYS2 used: stop programs still running from it, then clear read-only files."""
    if not tree.exists():
        return
    subprocess.run(
        [
            "powershell",
            "-NoProfile",
            "-Command",
            f"Get-Process | Where-Object {{ $_.Path -like '{PureWindowsPath(tree)}\\*' }}"
            " | Stop-Process -Force",
        ],
        capture_output=True,
    )

    def writable(function, name, _):
        os.chmod(name, stat.S_IWRITE)
        function(name)

    shutil.rmtree(tree, onexc=writable)


def build(arch, tree=TREE, jobs=None):
    """Make the whole tree from nothing; an existing one is replaced."""
    jobs = jobs or os.cpu_count() or 4
    remove(tree)
    downloads = ROOT / "build/toolchain-downloads"
    installer = fetch(
        LOCK["msys2"]["url"], LOCK["msys2"]["sha256"], downloads / "msys2-base.sfx.exe"
    )
    tree.mkdir(parents=True)
    subprocess.run([str(installer), "-y", f"-o{tree}"], check=True)
    pacman = "pacman --noconfirm --noprogressbar"
    # The first upgrade replaces pacman and MSYS2's runtime, then ends its own shell with an
    # error; the second upgrades everything else.
    bash(arch, f"{pacman} -Syuu", tree, tree, check=False)
    bash(arch, f"{pacman} -Syuu", tree, tree)
    bash(arch, f"{pacman} -S --needed " + " ".join(packages(arch)), tree, tree)
    bash(arch, f"{pacman} -Scc", tree, tree)
    # The keyring's agents would keep files of the tree open.
    bash(arch, "gpgconf --kill all", tree, tree, check=False)

    python = system(arch)["python"]
    archive = fetch(python["url"], python["sha256"], downloads / Path(python["url"]).name)
    with tarfile.open(archive) as source:
        source.extractall(tree, filter="data")
    # The archive's single folder is "python".
    subprocess.run([str(tree / "python/python.exe"), "-m", "venv", str(tree / "venv")], check=True)
    pip = [str(tree / "venv/Scripts/python.exe"), "-m", "pip", "install", "--no-cache-dir"]
    lock = ROOT / "requirements/ci-windows-py313.txt"
    subprocess.run([*pip, "--require-hashes", "-r", str(lock)], check=True)

    prefix = "C:/papo-ci/deps" if tree == TREE else (tree / "deps").as_posix()
    bash(
        arch,
        f'python dist/bootstrap.py --prefix "{prefix}" --cache "{(tree / "source-cache").as_posix()}"'
        f" --jobs {jobs}",
        ROOT,
        tree,
    )
    bash(
        arch,
        f'python dist/resources.py --output "{(tree / "resources").as_posix()}" --manifest'
        " dist/resources.json tests/fixtures/resources.json",
        ROOT,
        tree,
    )
    for leftover in [*tree.glob("dependency-*"), tree / "source-cache"]:
        shutil.rmtree(leftover, ignore_errors=True)
    inventory = subprocess.run(
        [str(tree / "msys64/usr/bin/bash.exe"), "-lc", "pacman -Q"],
        env=shell_environment(arch, tree),
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    (tree / "msys2-packages.txt").write_text(inventory, encoding="utf-8")


def pack(tree, output):
    """One gzip archive of the tree, made and unpacked by the tar that ships with Windows."""
    output.parent.mkdir(parents=True, exist_ok=True)
    tar = Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32/tar.exe"
    excluded = ["papo-ci/msys64/var/cache", "papo-ci/msys64/tmp", "papo-ci/msys64/home"]
    command = [str(tar), "-czf", str(output), "-C", str(tree.parent)]
    for item in excluded:
        command += ["--exclude", item]
    subprocess.run([*command, tree.name], check=True)


def unpack(archive, tree=TREE):
    remove(tree)
    tar = Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32/tar.exe"
    subprocess.run([str(tar), "-xzf", str(archive), "-C", str(tree.parent)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--arch", choices=sorted(LOCK["systems"]), required=True)
    parser.add_argument("--jobs", type=int)
    parser.add_argument("--pack", type=Path, help="also write the tree to this archive")
    args = parser.parse_args()
    if os.name != "nt":
        raise SystemExit("The Windows toolchain is built on Windows")
    build(args.arch, jobs=args.jobs)
    if args.pack:
        pack(TREE, args.pack)


if __name__ == "__main__":
    sys.exit(main())
