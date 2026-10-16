#!/usr/bin/env python3
"""Merge identical common assets, rejecting collisions between build jobs."""

from pathlib import Path
import shutil
import sys
from bootstrap import sha256


def assemble(source, destination):
    destination.mkdir(parents=True, exist_ok=True)
    for path in sorted(source.rglob("*")):
        if not path.is_file():
            continue
        target = destination / path.name
        if target.exists():
            if sha256(path) != sha256(target):
                raise RuntimeError(f"Nonidentical duplicate release asset: {path.name}")
        else:
            shutil.copy2(path, target)


if __name__ == "__main__":
    assemble(Path(sys.argv[1]), Path(sys.argv[2]))
