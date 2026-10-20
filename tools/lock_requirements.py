"""Regenerate native CI locks with uv's cross-platform dependency resolver."""

from importlib.metadata import version
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    if version("uv") != "0.13.1":
        raise SystemExit("Install requirements/lock-tools.txt before updating CI locks")
    for name, platform in (
        ("linux", "x86_64-unknown-linux-gnu"),
        ("windows", "x86_64-pc-windows-msvc"),
    ):
        subprocess.run(
            [
                sys.executable,
                "-m",
                "uv",
                "pip",
                "compile",
                "requirements-dev.txt",
                "--python-version",
                "3.13",
                "--python-platform",
                platform,
                "--generate-hashes",
                "--no-emit-index-url",
                "--output-file",
                f"requirements/ci-{name}-py313.txt",
            ],
            cwd=ROOT,
            check=True,
        )


if __name__ == "__main__":
    main()
