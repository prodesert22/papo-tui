#!/usr/bin/env python3
"""Install, upgrade and uninstall the per-user setup in a temporary directory."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def terminal_smoke(binary):
    env = dict(os.environ)
    env["PYTHONPATH"] = os.pathsep.join(
        str(ROOT / "tests" / name) for name in ("support", "terminal", "integration")
    )
    subprocess.run(
        [sys.executable, str(ROOT / "tests/terminal/test_installed.py"), str(binary)],
        env=env,
        check=True,
        timeout=120,
    )


def version(binary, expected, cwd):
    actual = subprocess.check_output(
        [str(binary), "--version"], text=True, cwd=cwd, timeout=20
    ).strip()
    if actual != "papo-tui " + expected:
        raise RuntimeError("Installed version differs")
    subprocess.run(
        [str(binary), "--help"], cwd=cwd, check=True, stdout=subprocess.DEVNULL, timeout=20
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--arch", required=True)
    args = parser.parse_args()
    stem = f"papo-tui-{args.version}-windows-{args.arch}"
    setup = (args.artifacts / (stem + "-setup.exe")).resolve()
    with tempfile.TemporaryDirectory(prefix="Papo instalação á ") as temp:
        root = Path(temp)
        portable = root / "portable"
        destination = root / "installed"
        with zipfile.ZipFile(args.artifacts / (stem + ".zip")) as archive:
            archive.extractall(portable)
        version(portable / "papo-tui.exe", args.version, root)
        terminal_smoke(portable / "papo-tui.exe")
        for _ in range(2):
            subprocess.run(
                [
                    str(setup),
                    "/VERYSILENT",
                    "/SUPPRESSMSGBOXES",
                    "/NORESTART",
                    "/CURRENTUSER",
                    "/DIR=" + str(destination),
                ],
                check=True,
                timeout=120,
            )
            version(destination / "papo-tui.exe", args.version, root)
        subprocess.run(
            [str(destination / "unins000.exe"), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"],
            check=True,
            timeout=120,
        )
        if (destination / "papo-tui.exe").exists():
            raise RuntimeError("Uninstall left the executable")


if __name__ == "__main__":
    main()
