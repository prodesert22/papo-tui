"""Regenerate native CI locks with uv's cross-platform dependency resolver."""

from importlib.metadata import version
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    if version("uv") != "0.13.1":
        raise SystemExit("Install requirements/lock-tools.txt before updating CI locks")
    platforms = {
        "linux": "x86_64-unknown-linux-gnu",
        "windows": "x86_64-pc-windows-msvc",
        "macos": "aarch64-apple-darwin",
    }
    # Naming one or more locks regenerates only those, so the others keep their pins.
    unknown = set(sys.argv[1:]) - set(platforms)
    if unknown:
        raise SystemExit(f"Unknown lock names: {sorted(unknown)}; choose from {sorted(platforms)}")
    for name, platform in platforms.items():
        if sys.argv[1:] and name not in sys.argv[1:]:
            continue
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
