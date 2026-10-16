#!/usr/bin/env python3
"""Configure the same dependency prefix and CMake build in CI or locally."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
from bootstrap import ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build/release")
    parser.add_argument("--prefix", type=Path, default=ROOT / "build/deps")
    parser.add_argument("--sanitizers", action="store_true")
    parser.add_argument("--coverage", action="store_true")
    args = parser.parse_args()
    prefix = args.prefix.resolve()
    build = args.build.resolve()
    subprocess.run(
        [sys.executable, str(ROOT / "dist/bootstrap.py"), "--prefix", str(prefix)], check=True
    )
    subprocess.run([sys.executable, str(ROOT / "dist/resources.py")], check=True)
    env = dict(os.environ)
    env["PATH"] = str(prefix / "bin") + os.pathsep + env["PATH"]
    env["PKG_CONFIG_PATH"] = (
        str(prefix / "lib/pkgconfig") + os.pathsep + env.get("PKG_CONFIG_PATH", "")
    )
    options = [
        "-DCMAKE_PREFIX_PATH=" + str(prefix),
        "-DCMAKE_BUILD_TYPE=Debug"
        if args.sanitizers or args.coverage
        else "-DCMAKE_BUILD_TYPE=Release",
        "-DPython3_EXECUTABLE=" + sys.executable,
        "-DPAPO_RELEASE_BUILD=ON",
        "-DPAPO_BUNDLE_RUNTIME=ON",
        "-DPAPO_DEPENDENCY_LICENSE_DIR=" + str(prefix / "share/licenses/papo-dependencies"),
        "-DPAPO_RUNTIME_SEARCH_DIRS="
        + str(prefix / "bin")
        + ";"
        + os.environ.get("PAPO_SYSTEM_BIN", ""),
    ]
    if os.name == "nt":
        options += ["-DCMAKE_INSTALL_BINDIR=."]
        platform_prefix = subprocess.check_output(
            ["cygpath", "-w", os.environ["MINGW_PREFIX"]], text=True
        ).strip()
        options += ["-DPAPO_PLATFORM_DATA_PREFIX=" + platform_prefix]
    if args.sanitizers:
        options += ["-DPAPO_SANITIZERS=ON"]
    if args.coverage:
        options += ["-DPAPO_COVERAGE=ON"]
    subprocess.run(
        ["cmake", "-S", str(ROOT), "-B", str(build), "-G", "Ninja", *options], env=env, check=True
    )
    subprocess.run(["cmake", "--build", str(build), "--parallel", "4"], env=env, check=True)
    subprocess.run(
        ["ctest", "--test-dir", str(build), "--output-on-failure", "--parallel", "2"],
        env=env,
        check=True,
    )


if __name__ == "__main__":
    main()
