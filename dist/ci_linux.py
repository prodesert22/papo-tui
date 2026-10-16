#!/usr/bin/env python3
"""Build Linux release targets concurrently with isolated source trees and CPU budgets."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
import subprocess
import sys
import time

from assemble import assemble
from bootstrap import ROOT
from build import positive

IMAGES = {"ubuntu": "ubuntu:24.04", "debian": "debian:13", "fedora": "fedora:44"}
TARGETS = [*IMAGES, "flatpak"]


def prepare(root):
    head = subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip()
    for name in TARGETS:
        source = root / name / "source"
        if not source.exists():
            source.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(["git", "clone", "--no-hardlinks", str(ROOT), str(source)], check=True)
        copied = subprocess.check_output(
            ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
        ).strip()
        if copied != head:
            raise RuntimeError(
                f"Stale generated checkout: {source}; remove it before preparing again"
            )


def commands(name, source, architecture, version, jobs):
    if name == "flatpak":
        env = dict(os.environ, PYTHON=sys.executable, PAPO_BUILD_JOBS=str(jobs))
        return [
            [
                "flatpak",
                "remote-add",
                "--user",
                "--if-not-exists",
                "flathub",
                "https://flathub.org/repo/flathub.flatpakrepo",
            ],
            [
                "flatpak",
                "install",
                "--user",
                "--noninteractive",
                "flathub",
                "org.freedesktop.Platform//26.08",
                "org.freedesktop.Sdk//26.08",
            ],
            ["dbus-run-session", "--", "sh", "dist/flatpak/build.sh", architecture, version],
        ], env
    command = [
        "docker",
        "run",
        "--rm",
        "--init",
        "--cpus",
        str(jobs),
        "--volume",
        str(source) + ":/workspace",
        "--workdir",
        "/workspace",
        "--env",
        "GITHUB_ACTIONS=true",
        "--env",
        "GITHUB_REPOSITORY",
        "--env",
        "SOURCE_DATE_EPOCH",
        "--env",
        f"PAPO_RUNNER_UID={os.getuid()}",
        "--env",
        f"PAPO_RUNNER_GID={os.getgid()}",
        "--env",
        "ASAN_OPTIONS=detect_leaks=1:abort_on_error=1",
        "--env",
        "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1",
        IMAGES[name],
        "sh",
        "dist/linux-release.sh",
        name,
        architecture,
        version,
        str(jobs),
    ]
    result = [command]
    if name == "ubuntu":
        package = source / "build/artifacts" / f"papo-tui-{version}-arch-{architecture}.pkg.tar.zst"
        if architecture == "x86_64":
            result.append(
                [
                    "docker",
                    "run",
                    "--rm",
                    "--volume",
                    str(package.parent) + ":/packages:ro",
                    "archlinux:base",
                    "sh",
                    "-ec",
                    'pacman -Syu --noconfirm; pacman -U --noconfirm "/packages/$1"; '
                    "papo-tui --version; papo-tui --help >/dev/null; pacman -R --noconfirm papo-tui",
                    "sh",
                    package.name,
                ]
            )
        else:
            result.append(["sh", str(ROOT / "dist/arch/verify.sh"), str(package)])
    return result, dict(os.environ)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--prepare", action="store_true", help="create isolated checkouts before restoring caches"
    )
    parser.add_argument("--arch", choices=["x86_64", "aarch64"])
    parser.add_argument("--version")
    parser.add_argument("--jobs", type=positive, default=min(os.cpu_count() or 2, 4))
    parser.add_argument("--workers", type=positive, default=2)
    args = parser.parse_args()
    root = ROOT / "build/release-tasks"
    prepare(root)
    if args.prepare:
        return
    if not args.arch or not args.version:
        parser.error("--arch and --version are required to build")
    workers = min(args.workers, args.jobs, len(TARGETS))
    jobs = max(1, args.jobs // workers)
    logs = root / "logs"
    logs.mkdir(exist_ok=True)
    (logs / "flatpak-sdk-ready").unlink(missing_ok=True)
    for name in IMAGES:
        (root / name / "source/build/release-deps/.papo-prepared").unlink(missing_ok=True)

    def run(name):
        source = root / name / "source"
        path = logs / (name + ".log")
        start = time.monotonic()
        print(f"Starting {name} ({jobs} CPUs): {path}", flush=True)
        code = 0
        try:
            with path.open("w") as log:
                tasks, env = commands(name, source, args.arch, args.version, jobs)
                for command in tasks:
                    subprocess.run(
                        command,
                        cwd=source,
                        env=env,
                        stdout=log,
                        stderr=subprocess.STDOUT,
                        check=True,
                    )
                    if command[:2] == ["flatpak", "install"]:
                        (logs / "flatpak-sdk-ready").touch()
        except (subprocess.CalledProcessError, OSError) as error:
            code = getattr(error, "returncode", 1)
            print(
                f"FAILED {name}: {error}\n"
                + "\n".join(path.read_text(errors="replace").splitlines()[-80:]),
                flush=True,
            )
        elapsed = round(time.monotonic() - start, 2)
        print(f"Finished {name}: exit={code}, elapsed={elapsed}s", flush=True)
        return {"target": name, "exit_code": code, "seconds": elapsed}

    # Independent distributions/Flatpak keep their own prefixes, ports and outputs.
    # Wait for every target so one failure cannot hide failures in other targets.
    with ThreadPoolExecutor(max_workers=workers) as pool:
        results = list(pool.map(run, TARGETS))
    (logs / "timings.json").write_text(json.dumps(results, indent=2) + "\n")
    if os.environ.get("GITHUB_OUTPUT"):
        ready = all(
            (root / name / "source/build/release-deps/.papo-prepared").is_file() for name in IMAGES
        )
        with open(os.environ["GITHUB_OUTPUT"], "a") as output:
            output.write(f"dependencies-ready={str(ready).lower()}\n")
            output.write(
                f"flatpak-sdk-ready={str((logs / 'flatpak-sdk-ready').is_file()).lower()}\n"
            )
    if any(result["exit_code"] for result in results):
        raise SystemExit(1)
    # Reuse the release assembler to reject differing common source assets.
    for name in TARGETS:
        assemble(root / name / "source/build/artifacts", ROOT / "build/artifacts")


if __name__ == "__main__":
    main()
