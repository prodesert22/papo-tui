#!/usr/bin/env python3
"""Run CI jobs inside the pinned toolchain; GitHub's workflow calls these same commands.

Only the standard library is used: this file runs on a bare host, before any environment
exists, and again inside the container, where it executes the job itself.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
REGISTRY = "ghcr.io/prodesert22/papo-tui-ci"
# Everything the image is made of. The tag is the hash of these files, so a change to any of
# them selects another image; tests/distribution checks the Dockerfile copies nothing else.
IMAGE_INPUTS = (
    "dist/ci/Dockerfile",
    "dist/ci/Dockerfile.dockerignore",
    "dist/ci/entrypoint.sh",
    "dist/linux-deps.sh",
    "dist/bootstrap.py",
    "dist/voice_dependencies.py",
    "dist/dependencies.json",
    "dist/patches",
    "dist/resources.py",
    "dist/resources.json",
    "tests/fixtures/resources.json",
    "requirements/ci-linux-py313.txt",
)
WORKSPACE = "/workspace"
PREFIX = "/opt/papo/deps"
# The job's environment is defined here, not in the workflow, so both callers share it.
ENVIRONMENT = {
    "PYTHONUTF8": "1",
    "PYTHONIOENCODING": "utf-8",
    "TZ": "UTC",
    "PAPO_TEST_REQUIRED": "1",
    "ASAN_OPTIONS": "detect_leaks=1:abort_on_error=1",
    "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1",
}
# Facts about the caller that scripts record or branch on; never credentials.
PASSTHROUGH = (
    "GITHUB_ACTIONS",
    "GITHUB_REPOSITORY",
    "GITHUB_EVENT_NAME",
    "ImageOS",
    "ImageVersion",
    "RUNNER_OS",
    "BASE_SHA",
    "SOURCE_DATE_EPOCH",
)
# A GitHub-hosted runner for a public repository: 4 CPUs and 16 GB.
RUNNER_CPUS = 4
RUNNER_MEMORY = "16g"
PLATFORMS = {"x86_64": "linux/amd64", "aarch64": "linux/arm64"}
# Job name -> the stages the workflow runs as separate steps.
JOBS = {
    "quality": ("test",),
    "linux-release": ("build", "test"),
    "linux-sanitizers": ("build", "test"),
    "linux-coverage": ("build", "test"),
}
# Windows jobs run in the archive of dist/windows/toolchain.py, on Windows or in the Windows VM.
WINDOWS_JOBS = {"windows-release": ("build", "test")}
SENSITIVE_SCENARIOS = (
    "test_grouping_survives_prepend_scroll_and_resize"
    "|test_graphical_server_badge_is_inside_tab_delimiters"
)


def native_arch():
    machine = platform.machine().lower()
    return {"amd64": "x86_64", "arm64": "aarch64"}.get(machine, machine)


def image_hash(root=ROOT, inputs=IMAGE_INPUTS):
    digest = hashlib.sha256()
    for name in inputs:
        path = root / name
        files = sorted(p for p in path.rglob("*") if p.is_file()) if path.is_dir() else [path]
        for file in files:
            if "__pycache__" in file.parts:
                continue
            digest.update(file.relative_to(root).as_posix().encode() + b"\0")
            digest.update(hashlib.sha256(file.read_bytes()).digest())
    return digest.hexdigest()


def image_tag(arch, root=ROOT):
    return f"{REGISTRY}:linux-{arch}-{image_hash(root)[:16]}"


def quiet(command):
    return subprocess.run(command, capture_output=True, text=True)


def image_identity(tag):
    """The registry digest when the image was pulled or pushed; otherwise its local id."""
    result = quiet(["docker", "image", "inspect", "--format", "{{json .RepoDigests}} {{.Id}}", tag])
    if result.returncode:
        return None
    digests, identifier = result.stdout.strip().rsplit(" ", 1)
    published = [item for item in json.loads(digests) if item.startswith(REGISTRY + "@")]
    return published[0].split("@", 1)[1] if published else "local:" + identifier


def build_image(tag, arch, root=ROOT):
    print(f"Building {tag}; later runs reuse it", flush=True)
    subprocess.run(
        [
            "docker",
            "buildx",
            "build",
            "--platform",
            PLATFORMS[arch],
            "--file",
            str(root / "dist/ci/Dockerfile"),
            "--tag",
            tag,
            "--load",
            str(root),
        ],
        check=True,
    )


def ensure_image(arch, root=ROOT, pull=True):
    """The published image when there is one, else a local build of the same inputs.

    A local build has the same tools but not the same bytes as the image GitHub runs, so a
    published tag replaces it; without the registry, the local image is used as it is.
    """
    tag = image_tag(arch, root)
    local = image_identity(tag)
    if pull:
        command = ["docker", "pull", "--quiet", "--platform", PLATFORMS[arch], tag]
        result = quiet(command) if local else subprocess.run(command)
        if result.returncode == 0:
            return tag
    if local:
        print(f"Using the local build of {tag}; it is not published yet", flush=True)
        return tag
    build_image(tag, arch, root)
    return tag


def publish_image(arch, root=ROOT):
    """Build and push only a tag the registry lacks; an existing tag is never replaced."""
    tag = image_tag(arch, root)
    if quiet(["docker", "manifest", "inspect", tag]).returncode == 0:
        print(f"Already published: {tag}", flush=True)
        return tag
    if not image_identity(tag):
        build_image(tag, arch, root)
    subprocess.run(["docker", "push", tag], check=True)
    return tag


def git_common_mount(source):
    """A worktree keeps its objects in the main checkout, which the container must also see."""
    result = quiet(
        ["git", "-C", str(source), "rev-parse", "--path-format=absolute", "--git-common-dir"]
    )
    if result.returncode:
        return []
    common = Path(result.stdout.strip())
    if common.is_relative_to(source):
        return []
    return ["--volume", f"{common}:{common}:ro"]


def cpu_set(slot, cpus=None):
    """Four CPUs of the host for this job, so every program counts what a runner has."""
    cpus = cpus or os.cpu_count() or 1
    if cpus < RUNNER_CPUS:
        return None
    first = (slot * RUNNER_CPUS) % (cpus - cpus % RUNNER_CPUS)
    return f"{first}-{first + RUNNER_CPUS - 1}"


def container_command(
    job,
    stage,
    tag,
    source,
    *,
    arch,
    fast=False,
    full=False,
    slot=0,
    extra=(),
    name=None,
    identity=None,
    environment=None,
):
    """The docker invocation for one job stage; identical on GitHub and on a developer's host."""
    environment = os.environ if environment is None else environment
    command = ["docker", "run", "--rm", "--init", "--platform", PLATFORMS[arch]]
    if name:
        command += ["--name", name]
    if not fast:
        cpus = cpu_set(slot)
        command += ["--cpuset-cpus", cpus] if cpus else ["--cpus", str(RUNNER_CPUS)]
        command += ["--memory", RUNNER_MEMORY]
    variables = dict(
        ENVIRONMENT,
        PAPO_UID=str(os.getuid()),
        PAPO_GID=str(os.getgid()),
        PAPO_TOOLCHAIN=tag,
        PAPO_TOOLCHAIN_DIGEST=identity or "",
    )
    for key in PASSTHROUGH:
        if key in environment:
            variables[key] = environment[key]
    for key, value in variables.items():
        command += ["--env", f"{key}={value}"]
    command += ["--volume", f"{source}:{WORKSPACE}", *git_common_mount(source)]
    command += ["--workdir", WORKSPACE, tag, "python", "tools/ci.py", "inside", job]
    command += ["--stage", stage, "--arch", arch]
    if fast:
        command += ["--fast"]
    if full:
        command += ["--full"]
    if extra:
        command += ["--", *extra]
    return command


def committed_source(root=ROOT):
    """A clean clone of HEAD: what GitHub checks out, without local or ignored files."""
    source = root / "build/toolchain/committed/source"
    if source.exists():
        shutil.rmtree(source)
    source.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["git", "clone", "--quiet", "--no-hardlinks", str(root), str(source)], check=True
    )
    head = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    subprocess.run(["git", "-C", str(source), "checkout", "--quiet", "--detach", head], check=True)
    return source


def run_windows_job(args, source):
    """The Windows job: here on Windows, otherwise in the Windows VM."""
    sys.path.insert(0, str(source / "tools"))
    import ci_windows

    stages = [stage for stage in WINDOWS_JOBS["windows-release"] if args.stage in ("all", stage)]
    started = time.monotonic()
    if os.name != "nt":
        code = ci_windows.run_in_vm(args.arch, args.stage, args.extra, args.fast)
        return "windows-release", code, time.monotonic() - started
    for stage in stages:
        code = ci_windows.run_windows(args.arch, stage, args.extra, args.fast)
        if code:
            return "windows-release", code, time.monotonic() - started
    return "windows-release", 0, time.monotonic() - started


def run_jobs(args):
    known = {**JOBS, **WINDOWS_JOBS}
    # Docker runs the Linux jobs; a Windows host runs only its own.
    jobs = args.jobs or (list(WINDOWS_JOBS) if os.name == "nt" else list(JOBS))
    unknown = [job for job in jobs if job not in known]
    if unknown:
        raise SystemExit(f"Unknown jobs {unknown}; choose from {list(known)}")
    source = committed_source() if args.committed else ROOT
    if os.name == "nt" and set(jobs) - set(WINDOWS_JOBS):
        raise SystemExit("Linux jobs need Docker; run them on a Linux host")
    windows = [job for job in jobs if job in WINDOWS_JOBS]
    jobs = [job for job in jobs if job not in WINDOWS_JOBS]
    if not jobs:
        results = [run_windows_job(args, source)]
        for job, code, seconds in results:
            print(f"{'passed' if code == 0 else 'FAILED':7}  {job}  {seconds:.0f}s", flush=True)
        raise SystemExit(1 if results[0][1] else 0)
    tag = ensure_image(args.arch, source)
    identity = image_identity(tag)
    print(f"Toolchain {tag} ({identity})", flush=True)
    if args.fresh:
        for job in jobs:
            if job.startswith("linux-"):
                variant = source / "build/toolchain" / args.arch / job.removeprefix("linux-")
                shutil.rmtree(variant, ignore_errors=True)
    logs = source / "build/toolchain/logs"
    logs.mkdir(parents=True, exist_ok=True)

    def run(slot, job):
        stages = [stage for stage in JOBS[job] if args.stage in ("all", stage)]
        started = time.monotonic()
        if not stages:
            return job, None, 0
        for stage in stages:
            name = f"papo-ci-{job}-{stage}-{os.getpid()}"
            command = container_command(
                job,
                stage,
                tag,
                source,
                arch=args.arch,
                fast=args.fast,
                full=args.full,
                slot=slot,
                extra=args.extra,
                name=name,
                identity=identity,
            )
            try:
                if len(jobs) == 1:
                    code = subprocess.run(command).returncode
                else:
                    path = logs / f"{job}-{stage}.log"
                    with path.open("w", encoding="utf-8") as log:
                        code = subprocess.run(
                            command, stdout=log, stderr=subprocess.STDOUT
                        ).returncode
                    if code:
                        tail = (
                            path.read_text(encoding="utf-8", errors="replace").splitlines()
                            if path.exists()
                            else [f"(no log: {path})"]
                        )
                        print(f"FAILED {job} {stage}; {path}\n" + "\n".join(tail[-60:]), flush=True)
            except KeyboardInterrupt:
                quiet(["docker", "kill", name])
                raise
            if code:
                return job, code, time.monotonic() - started
        return job, 0, time.monotonic() - started

    if len(jobs) == 1 and not windows:
        results = [run(0, jobs[0])]
    else:
        print(f"Running {', '.join(jobs + windows)} together; logs in {logs}", flush=True)
        with ThreadPoolExecutor(max_workers=len(jobs) + len(windows)) as pool:
            futures = [pool.submit(run, slot, job) for slot, job in enumerate(jobs)]
            if windows:
                futures.append(pool.submit(run_windows_job, args, source))
            results = [future.result() for future in futures]
    for job, code, seconds in results:
        state = "skipped" if code is None else "passed" if code == 0 else "FAILED"
        print(f"{state:7}  {job}  {seconds:.0f}s", flush=True)
    if any(code for _, code, _ in results):
        raise SystemExit(1)


def group(title):
    if os.environ.get("GITHUB_ACTIONS") == "true":
        print(f"::group::{title}", flush=True)
    else:
        print(f"== {title}", flush=True)


def end_group():
    if os.environ.get("GITHUB_ACTIONS") == "true":
        print("::endgroup::", flush=True)


def quality():
    """Independent gates run together; every failure is reported, not only the first."""
    python = sys.executable
    checks = {
        "formatting and lint": [python, "tools/quality.py"],
        "contract tests": [python, "-m", "unittest", "discover", "-s", "tests/contract", "-v"],
        "distribution tests": [
            python,
            "-m",
            "unittest",
            "discover",
            "-s",
            "tests/distribution",
            "-v",
        ],
        "workflows": ["actionlint"],
    }
    if os.environ.get("BASE_SHA"):
        checks["version change"] = [
            python,
            "dist/release.py",
            "--changed-since",
            os.environ["BASE_SHA"],
        ]

    def run(command):
        return subprocess.run(
            command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace"
        )

    with ThreadPoolExecutor(max_workers=len(checks)) as pool:
        results = dict(zip(checks, pool.map(run, checks.values())))
    for title, result in results.items():
        group(f"{title}: {'passed' if result.returncode == 0 else 'FAILED'}")
        print(result.stdout, end="", flush=True)
        end_group()
    failed = [title for title, result in results.items() if result.returncode]
    if failed:
        raise SystemExit("Failed: " + ", ".join(failed))


def inside(args):
    """Executed in the container, as the job's unprivileged user."""
    if args.job == "quality":
        return quality()
    variant = args.job.removeprefix("linux-")
    build = Path("build/toolchain") / args.arch
    command = [sys.executable, "dist/build.py", "--build", str(build), "--prefix", PREFIX]
    command += ["--variants", variant]
    # Python reports the host's CPUs even when the container is confined to four.
    cpus = os.cpu_count() if args.fast else RUNNER_CPUS
    command += ["--jobs", str(cpus)]
    if not args.fast:
        command += ["--test-jobs", str(RUNNER_CPUS)]
    if args.stage == "build":
        raise SystemExit(subprocess.run(command + ["--build-only"]).returncode)
    command += ["--test-only", *args.extra]
    if args.full and variant != "release":
        command += ["--full-instrumentation"]
    status = subprocess.run(command).returncode
    directory = build / variant
    if variant == "coverage":
        # Static analysis and the report do not depend on the tests having passed.
        if (directory / "compile_commands.json").is_file():
            group("static analysis")
            analysis = [sys.executable, "tools/analyze.py", "--build", str(directory)]
            status = subprocess.run(analysis).returncode or status
            end_group()
        else:
            status = status or 1
        if (directory / "CMakeCache.txt").is_file():
            group("coverage report")
            # Only this build's counters: the checkout may hold other, native build trees.
            report = ["gcovr", "--root", ".", "--filter", "src/"]
            report += ["--exclude", "src/model/text/emoji_data.inc"]
            report += ["--html-details", str(directory / "index.html")]
            report += ["--xml", str(directory / "coverage.xml"), "--print-summary"]
            report += [str(directory)]
            status = subprocess.run(report).returncode or status
            end_group()
    if variant == "sanitizers" and args.full:
        group("sensitive scenarios, repeated")
        repeat = ["ctest", "--test-dir", str(directory), "--output-on-failure", "--no-tests=error"]
        repeat += ["--parallel", "2", "--repeat", "until-fail:10", "-R", SENSITIVE_SCENARIOS]
        status = subprocess.run(repeat).returncode or status
        end_group()
    raise SystemExit(status)


def shell(args):
    tag = ensure_image(args.arch)
    command = container_command(
        "quality", "test", tag, ROOT, arch=args.arch, fast=True, identity=image_identity(tag)
    )
    # Replace the job with an interactive shell in the same environment.
    command = command[: command.index(tag)]
    command[2:2] = ["--interactive", "--tty"]
    raise SystemExit(subprocess.run(command + [tag, "bash"]).returncode)


COMPARED = ("cmake", "compiler", "ffmpeg", "fonts", "python_dependencies")


def environment_facts(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    facts = {key: data["tools"].get(key) for key in COMPARED}
    facts["python"] = data.get("python")
    facts["toolchain"] = data.get("toolchain")
    return facts


def compare(args):
    """Prove a GitHub run and the local run used the same tools, from what both recorded."""
    local = ROOT / "build/toolchain" / args.arch / args.variant / "ci-logs/environment.json"
    if not local.is_file():
        raise SystemExit(f"No local record at {local}; run the job's test stage first")
    artifact = (
        f"build-results-linux-{args.arch}"
        if args.variant == "release"
        else f"instrumentation-{args.variant}"
    )
    with tempfile.TemporaryDirectory() as directory:
        subprocess.run(
            ["gh", "run", "download", args.run, "--name", artifact, "--dir", directory], check=True
        )
        remote = [
            path
            for path in Path(directory).rglob("environment.json")
            if path.parent.parent.name == args.variant
        ]
        if not remote:
            raise SystemExit(f"Run {args.run} recorded no environment for {args.variant}")
        theirs, ours = environment_facts(remote[0]), environment_facts(local)
    different = [key for key in ours if ours[key] != theirs[key]]
    for key in ours:
        print(f"{'DIFFERENT' if key in different else 'same     '}  {key}")
    for key in different:
        print(f"\n{key}\n  local:  {ours[key]!r}\n  GitHub: {theirs[key]!r}")
    if different:
        raise SystemExit(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)

    def with_arch(sub):
        sub.add_argument("--arch", choices=sorted(PLATFORMS), default=native_arch())
        return sub

    image = with_arch(commands.add_parser("image", help="pull or build the toolchain image"))
    image.add_argument("--push", action="store_true", help="publish the tag when it is missing")
    image.add_argument("--print", action="store_true", help="only print the tag")
    image.add_argument("--windows", action="store_true", help="the Windows toolchain archive")

    run = with_arch(
        commands.add_parser(
            "run",
            help="run jobs; all of them when none is named",
            epilog="Options after -- go to dist/build.py's test stage, such as --case or --suite.",
        )
    )
    run.add_argument("jobs", nargs="*", metavar="JOB", help=", ".join([*JOBS, *WINDOWS_JOBS]))
    run.add_argument("--stage", choices=("all", "build", "test"), default="all")
    run.add_argument("--fast", action="store_true", help="use every CPU instead of a runner's four")
    run.add_argument("--full", action="store_true", help="the scheduled, fully instrumented run")
    run.add_argument("--committed", action="store_true", help="test a clean clone of HEAD")
    run.add_argument("--fresh", action="store_true", help="discard the previous build tree")

    job = with_arch(commands.add_parser("inside"))
    job.add_argument("job", choices=sorted(JOBS))
    job.add_argument("--stage", choices=("build", "test"), required=True)
    job.add_argument("--fast", action="store_true")
    job.add_argument("--full", action="store_true")

    with_arch(commands.add_parser("shell", help="an interactive shell in the toolchain"))

    script = with_arch(
        commands.add_parser("exec", help="Windows: run a bash script in the toolchain's shell")
    )
    script.add_argument("script", help="path of the script in the checkout")

    check = with_arch(commands.add_parser("compare", help="compare tools with a GitHub run"))
    check.add_argument("run", help="GitHub run id")
    check.add_argument(
        "--variant", choices=("release", "sanitizers", "coverage"), default="release"
    )

    # Everything after "--" goes to dist/build.py's test stage (--case, --suite, --area...).
    arguments = sys.argv[1:]
    extra = []
    if "--" in arguments:
        extra = arguments[arguments.index("--") + 1 :]
        arguments = arguments[: arguments.index("--")]
    args = parser.parse_args(arguments)
    args.extra = extra
    if args.command == "image" and args.windows:
        import ci_windows

        if args.print:
            print(ci_windows.toolchain_tag(args.arch))
        elif args.push:
            ci_windows.publish_toolchain(args.arch)
        else:
            print(ci_windows.ensure_toolchain(args.arch))
    elif args.command == "image":
        if args.print:
            print(image_tag(args.arch))
        elif args.push:
            publish_image(args.arch)
        else:
            print(ensure_image(args.arch))
    elif args.command == "run":
        run_jobs(args)
    elif args.command == "inside":
        inside(args)
    elif args.command == "shell":
        shell(args)
    elif args.command == "exec":
        import ci_windows

        raise SystemExit(ci_windows.run_script(args.arch, args.script))
    else:
        compare(args)


if __name__ == "__main__":
    main()
