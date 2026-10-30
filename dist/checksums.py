#!/usr/bin/env python3
"""Generate or verify SHA256SUMS for files directly inside an artifact directory."""

import argparse
import hashlib
import os
from pathlib import Path
import re
import tempfile

MANIFEST = "SHA256SUMS"


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_name(name):
    if (
        not name
        or name in (".", "..", MANIFEST)
        or "/" in name
        or "\\" in name
        or not name.isprintable()
    ):
        raise ValueError(f"Unsupported artifact name: {name!r}")


def artifact_names(directory):
    names = []
    for path in directory.iterdir():
        if path.name == MANIFEST:
            continue
        if path.is_symlink():
            raise ValueError(f"Artifact must not be a symbolic link: {path.name!r}")
        if path.is_dir():
            continue
        if not path.is_file():
            raise ValueError(f"Artifact must be a regular file: {path.name!r}")
        validate_name(path.name)
        names.append(path.name)
    if not names:
        raise ValueError("No artifact files found")
    return sorted(names)


def write_manifest(directory):
    directory = Path(directory)
    names = artifact_names(directory)
    contents = "".join(f"{sha256(directory / name)}  {name}\n" for name in names)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", newline="\n", dir=directory, delete=False
        ) as output:
            temporary = Path(output.name)
            output.write(contents)
        os.replace(temporary, directory / MANIFEST)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return len(names)


def check_manifest(directory):
    directory = Path(directory)
    entries = {}
    for line in (directory / MANIFEST).read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
        if not match:
            raise ValueError("Invalid SHA256SUMS entry")
        digest, name = match.groups()
        validate_name(name)
        if name in entries:
            raise ValueError(f"Duplicate checksum: {name!r}")
        entries[name] = digest
    actual = set(artifact_names(directory))
    if actual != set(entries):
        raise ValueError(
            f"Artifacts differ: missing={sorted(set(entries) - actual)}, "
            f"extra={sorted(actual - set(entries))}"
        )
    for name, digest in entries.items():
        if sha256(directory / name) != digest:
            raise ValueError(f"Checksum mismatch: {name!r}")
    return len(entries)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", nargs="?", type=Path, default=Path("build/artifacts"))
    parser.add_argument("--check", action="store_true", help="verify the existing manifest")
    args = parser.parse_args()
    try:
        count = check_manifest(args.directory) if args.check else write_manifest(args.directory)
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")
    action = "Verified" if args.check else "Created"
    print(f"{action} {args.directory / MANIFEST} ({count} files)")


if __name__ == "__main__":
    main()
