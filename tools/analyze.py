#!/usr/bin/env python3
"""Run Clang's path-sensitive analyzer on compiled application sources."""

import argparse
import json
from pathlib import Path
import subprocess
from concurrent.futures import ThreadPoolExecutor


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument(
        "--clang-tidy", default="clang-tidy-18", help="pinned Clang 18 analyzer executable"
    )
    args = parser.parse_args()
    build = args.build.resolve()
    commands = json.loads((build / "compile_commands.json").read_text())
    sources = sorted(
        {entry["file"] for entry in commands if "/src/" in entry["file"].replace("\\", "/")}
    )

    def analyze(source):
        result = subprocess.run(
            [
                args.clang_tidy,
                "-p",
                str(build),
                "-quiet",
                "-checks=-*,clang-analyzer-core.*,clang-analyzer-unix.*,clang-analyzer-deadcode.*,"
                "clang-analyzer-security.*,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling",
                "-warnings-as-errors=clang-analyzer-*",
                source,
            ],
            capture_output=True,
            text=True,
        )
        return source, result

    failed = False
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for source, result in pool.map(analyze, sources):
            if result.returncode:
                failed = True
                print(source, result.stdout, result.stderr)
    print(f"Analyzed {len(sources)} translation units")
    raise SystemExit(1 if failed else 0)


if __name__ == "__main__":
    main()
