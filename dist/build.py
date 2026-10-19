#!/usr/bin/env python3
"""Configure the same dependency prefix and CMake build in CI or locally."""

import argparse
import hashlib
import os
import json
import platform
import re
import time
from pathlib import Path
import subprocess
import sys
from bootstrap import ROOT


def build_variant(
    build,
    prefix,
    mode,
    jobs,
    test_jobs,
    log=None,
    *,
    suite="all",
    case=None,
    test_only=False,
    full_instrumentation=False,
):
    env = dict(os.environ)
    env["PATH"] = str(prefix / "bin") + os.pathsep + env["PATH"]
    env["PKG_CONFIG_PATH"] = (
        str(prefix / "lib/pkgconfig") + os.pathsep + env.get("PKG_CONFIG_PATH", "")
    )
    env.update(PYTHONUTF8="1", PYTHONIOENCODING="utf-8", TZ="UTC")
    options = [
        "-DBUILD_TESTING=ON",
        "-DPAPO_SANITIZERS=" + ("ON" if mode == "sanitizers" else "OFF"),
        "-DPAPO_COVERAGE=" + ("ON" if mode == "coverage" else "OFF"),
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
        # The private ncurses library does not install its own terminal database.
        # Installed packages discover their bundled data through media_resources_init.
        env.setdefault("TERMINFO", str(Path(platform_prefix) / "share/terminfo"))
        options += ["-DPAPO_PLATFORM_DATA_PREFIX=" + platform_prefix]
    if mode == "sanitizers":
        options += ["-DPAPO_SANITIZERS=ON"]
    if mode == "coverage":
        options += ["-DPAPO_COVERAGE=ON"]
    reports = build / "ci-logs"
    reports.mkdir(parents=True, exist_ok=True)
    stages = []

    def run(stage, command):
        started = time.monotonic()
        try:
            subprocess.run(
                command, env=env, check=True, stdout=log, stderr=subprocess.STDOUT if log else None
            )
        finally:
            stages.append({"stage": stage, "seconds": time.monotonic() - started})
            (reports / "stages.json").write_text(json.dumps(stages, indent=2), encoding="utf-8")

    if not test_only:
        run("configure", ["cmake", "-S", str(ROOT), "-B", str(build), "-G", "Ninja", *options])
        run("build", ["cmake", "--build", str(build), "--parallel", str(jobs)])
    run(
        "inventory",
        [sys.executable, str(ROOT / "tools/check_test_inventory.py"), "--build", str(build)],
    )
    metadata = {
        "platform": platform.platform(),
        "architecture": platform.machine(),
        "python": sys.version,
        "profile": mode,
        "suite": suite,
        "build_jobs": jobs,
        "test_jobs": test_jobs,
        "tools": {},
        "cache_hit": os.environ.get("PAPO_CI_CACHE_HIT", "unknown"),
        "runner_image": {
            key: os.environ.get(key) for key in ("ImageOS", "ImageVersion", "RUNNER_OS")
        },
    }
    for tool, command in {
        "cmake": ["cmake", "--version"],
        "compiler": [os.environ.get("CC", "cc"), "--version"],
        "ffmpeg": ["ffmpeg", "-version"],
        "fonts": ["fc-match", "Sans Bold"],
        "commit": ["git", "rev-parse", "HEAD"],
        "python_dependencies": [sys.executable, "-m", "pip", "freeze"],
    }.items():
        try:
            value = subprocess.run(
                command,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=10,
                env=env,
            )
            metadata["tools"][tool] = value.stdout.strip()
        except (OSError, subprocess.TimeoutExpired) as error:
            metadata["tools"][tool] = type(error).__name__
    difference = subprocess.run(["git", "diff", "HEAD", "--binary"], capture_output=True, cwd=ROOT)
    metadata["tracked_diff_sha256"] = hashlib.sha256(difference.stdout).hexdigest()
    metadata["tracked_source_dirty"] = bool(difference.stdout)
    (reports / "environment.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    command = [
        "ctest",
        "--test-dir",
        str(build),
        "--output-on-failure",
        "--no-tests=error",
        "--parallel",
        str(test_jobs),
        "--output-junit",
        str(reports / "results.xml"),
    ]
    if suite != "all":
        command += ["-L", "^" + suite + "$"]
    elif not full_instrumentation:
        if mode == "sanitizers":
            command += ["-L", "^(unit|integration|lifecycle)$"]
        elif mode == "coverage":
            command += ["-L", "^(unit|integration)$"]
    if case:
        command += ["-R", "^" + re.escape(case) + "$"]
    started = time.monotonic()
    try:
        run("test", command)
    finally:
        if (reports / "results.xml").is_file():
            sys.path.insert(0, str(ROOT / "tools"))
            from test_report import summarize

            summarize(reports / "results.xml", reports, time.monotonic() - started)


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
        help="CTest process budget (default: up to four native CPUs)",
    )
    parser.add_argument(
        "--suite",
        choices=("all", "unit", "integration", "terminal", "media", "contract", "lifecycle"),
        default="all",
    )
    parser.add_argument("--case", help="exact CTest scenario identifier")
    parser.add_argument(
        "--list-tests",
        action="store_true",
        help="list existing configured tests without building or running",
    )
    parser.add_argument(
        "--test-only",
        action="store_true",
        help="test an existing build without preparing or compiling",
    )
    parser.add_argument(
        "--full-instrumentation",
        action="store_true",
        help="also run every terminal scenario under instrumentation",
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
    if args.list_tests:
        directory = build / variants[0] if args.variants else build
        command = ["ctest", "--test-dir", str(directory), "-N", "--show-only=json-v1"]
        if args.suite != "all":
            command += ["-L", "^" + args.suite + "$"]
        subprocess.run(command, check=True)
        return
    if not args.test_only:
        prefix.mkdir(parents=True, exist_ok=True)
        ready = prefix / ".papo-prepared"
        ready.unlink(missing_ok=True)
        started = time.monotonic()
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
        build.mkdir(parents=True, exist_ok=True)
        (build / "preparation.json").write_text(
            json.dumps({"seconds": time.monotonic() - started}), encoding="utf-8"
        )
        ready.touch()
        if os.environ.get("GITHUB_OUTPUT"):
            with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
                output.write("dependencies-ready=true\n")
    logs = build / "ci-logs"
    logs.mkdir(parents=True, exist_ok=True)
    passed = True
    # CI puts each variant on its own runner. Local variants run sequentially,
    # so CPU/memory budgets never multiply through nested concurrency.
    for variant in variants:
        directory = build / variant if args.variants else build
        path = logs / (variant + ".log")
        try:
            with path.open("w", encoding="utf-8") as log:
                build_variant(
                    directory,
                    prefix,
                    variant,
                    args.jobs,
                    args.test_jobs or min(os.cpu_count() or 2, 4),
                    log,
                    suite=args.suite,
                    case=args.case,
                    test_only=args.test_only,
                    full_instrumentation=args.full_instrumentation,
                )
        except (subprocess.CalledProcessError, OSError) as error:
            passed = False
            print(
                f"FAILED {variant}: {error}; {path}\n"
                + "\n".join(path.read_text(encoding="utf-8", errors="replace").splitlines()[-80:]),
                flush=True,
            )
        else:
            print(f"Passed {variant}; log={path}", flush=True)
    if not passed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
