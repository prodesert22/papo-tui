#!/usr/bin/env python3
"""Read-only local/CI quality checks; generated and third-party code is excluded."""

import argparse
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--format", action="store_true", help="apply formatting explicitly")
    args = parser.parse_args()
    for tool in ("clang-format", "cmake-format", "ruff"):
        if not shutil.which(tool):
            raise SystemExit(f"Required tool unavailable: {tool}")
    sources = sorted(
        str(p)
        for folder in ("src", "tests")
        for p in (ROOT / folder).rglob("*")
        if p.suffix in (".c", ".h")
    )
    flags = ["-i"] if args.format else ["--dry-run", "--Werror"]
    subprocess.run(["clang-format", *flags, *sources], check=True, cwd=ROOT)
    cmake = [str(ROOT / "CMakeLists.txt"), *map(str, sorted((ROOT / "cmake").glob("*.cmake")))]
    subprocess.run(
        ["cmake-format", "-i" if args.format else "--check", *cmake], check=True, cwd=ROOT
    )
    python = [str(ROOT / folder) for folder in ("tests", "tools", "dist")]
    subprocess.run(["ruff", "format", *([] if args.format else ["--check"]), *python], check=True)
    subprocess.run(["ruff", "check", *python], check=True)


if __name__ == "__main__":
    main()
