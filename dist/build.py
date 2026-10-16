#!/usr/bin/env python3
"""Configure the same dependency prefix and CMake build in CI or locally."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import subprocess
import sys
from bootstrap import ROOT


def build_variant(build, prefix, mode, jobs, test_jobs, log=None):
    env = dict(os.environ)
    env["PATH"] = str(prefix / "bin") + os.pathsep + env["PATH"]
    env["PKG_CONFIG_PATH"] = (
        str(prefix / "lib/pkgconfig") + os.pathsep + env.get("PKG_CONFIG_PATH", "")
    )
    options = [
        "-DCMAKE_PREFIX_PATH=" + str(prefix),
        "-DCMAKE_BUILD_TYPE=Debug" if mode != "release" else "-DCMAKE_BUILD_TYPE=Release",
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
    if mode == "sanitizers":
        options += ["-DPAPO_SANITIZERS=ON"]
    if mode == "coverage":
        options += ["-DPAPO_COVERAGE=ON"]
    subprocess.run(
        ["cmake", "-S", str(ROOT), "-B", str(build), "-G", "Ninja", *options],
        env=env,
        check=True,
        stdout=log,
        stderr=subprocess.STDOUT if log else None,
    )
    subprocess.run(
        ["cmake", "--build", str(build), "--parallel", str(jobs)],
        env=env,
        check=True,
        stdout=log,
        stderr=subprocess.STDOUT if log else None,
    )
    subprocess.run(
        ["ctest", "--test-dir", str(build), "--output-on-failure", "--parallel", str(test_jobs)],
        env=env,
        check=True,
        stdout=log,
        stderr=subprocess.STDOUT if log else None,
    )


def positive(value):
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("must be at least one")
    return number


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build/release")
    parser.add_argument("--prefix", type=Path, default=ROOT / "build/deps")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--sanitizers", action="store_true")
    mode.add_argument("--coverage", action="store_true")
    mode.add_argument(
        "--variants", help="comma-separated release,sanitizers,coverage; --build is their parent"
    )
    parser.add_argument(
        "--jobs", type=positive, default=min(os.cpu_count() or 2, 4), help="total CPU budget"
    )
    parser.add_argument(
        "--test-jobs",
        type=positive,
        help="total CTest concurrency budget (default: two I/O tests per variant)",
    )
    args = parser.parse_args()
    variants = (
        args.variants.split(",")
        if args.variants
        else ["sanitizers" if args.sanitizers else "coverage" if args.coverage else "release"]
    )
    if len(set(variants)) != len(variants) or set(variants) - {"release", "sanitizers", "coverage"}:
        parser.error("variants must be unique release,sanitizers,coverage names")
    prefix, build = args.prefix.resolve(), args.build.resolve()
    prefix.mkdir(parents=True, exist_ok=True)
    ready = prefix / ".papo-prepared"
    ready.unlink(missing_ok=True)
    # A shared prefix is prepared exactly once, before any concurrent readers.
    subprocess.run(
        [
            sys.executable,
            str(ROOT / "dist/bootstrap.py"),
            "--prefix",
            str(prefix),
            "--jobs",
            str(args.jobs),
        ],
        check=True,
    )
    subprocess.run([sys.executable, str(ROOT / "dist/resources.py")], check=True)
    ready.touch()
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a") as output:
            output.write("dependencies-ready=true\n")
    workers = min(len(variants), args.jobs, args.test_jobs or len(variants))
    jobs = max(1, args.jobs // workers)
    test_jobs = max(1, (args.test_jobs or 2 * workers) // workers)
    if not args.variants:
        build_variant(build, prefix, variants[0], jobs, test_jobs)
        return
    logs = build / "ci-logs"
    logs.mkdir(parents=True, exist_ok=True)

    def run(mode):
        path = logs / (mode + ".log")
        print(f"Starting {mode}: build jobs={jobs}, test jobs={test_jobs}; log={path}", flush=True)
        try:
            with path.open("w") as log:
                build_variant(build / mode, prefix, mode, jobs, test_jobs, log)
        except (subprocess.CalledProcessError, OSError) as error:
            print(
                f"FAILED {mode}: {error}; {path}\n"
                + "\n".join(path.read_text(errors="replace").splitlines()[-80:]),
                flush=True,
            )
            return False
        print(f"Passed {mode}", flush=True)
        return True

    with ThreadPoolExecutor(max_workers=workers) as pool:
        results = list(pool.map(run, variants))
    if not all(results):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
