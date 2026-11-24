"""Windows CI jobs in the pinned toolchain of dist/windows/toolchain.py; used by tools/ci.py.

On Windows the job runs here, in C:\\papo-ci with the checkout reached through C:\\papo-ci\\src,
the same paths on a GitHub runner and on the Windows VM. On a Linux host it is handed to that VM
through its shared folder. Only the standard library is used.
"""

import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
TREE = Path("C:/papo-ci")
SOURCE = TREE / "src"
PREFIX = "C:/papo-ci/deps"
# Everything the tree is made of; its hash names the published archive.
TOOLCHAIN_INPUTS = (
    "dist/windows/toolchain.json",
    "dist/windows/toolchain.py",
    "dist/bootstrap.py",
    "dist/voice_dependencies.py",
    "dist/dependencies.json",
    "dist/patches",
    "dist/resources.py",
    "dist/resources.json",
    "tests/fixtures/resources.json",
    "requirements/ci-windows-py313.txt",
)
MEDIA_TYPE = "application/vnd.papo.toolchain.tar+gzip"
ARCHIVE = "toolchain.tar.gz"
VM_POLL_SECONDS = 15
VM_CONTAINER = os.environ.get("PAPO_WINDOWS_CONTAINER", "papo-windows")


def native_arch():
    machine = (os.environ.get("PROCESSOR_ARCHITECTURE") or platform.machine()).lower()
    return "aarch64" if machine in ("arm64", "aarch64") else "x86_64"


def toolchain_tag(arch, root=ROOT):
    from ci import REGISTRY, image_hash

    return f"{REGISTRY}:windows-{arch}-{image_hash(root, TOOLCHAIN_INPUTS)[:16]}"


def oras():
    """The pinned registry client, unpacked once into the build folder."""
    tools = json.loads((ROOT / "dist/tools.json").read_text(encoding="utf-8"))["oras"]
    system = "windows" if os.name == "nt" else "linux"
    machine = {"x86_64": "amd64", "aarch64": "arm64"}[native_arch()]
    asset = next(a for a in tools["assets"] if f"_{system}_{machine}." in a["name"])
    folder = ROOT / "build/tool-cache" / ("oras-" + tools["version"])
    program = folder / ("oras.exe" if os.name == "nt" else "oras")
    if program.exists():
        return program
    folder.mkdir(parents=True, exist_ok=True)
    archive = folder / asset["name"]
    with urllib.request.urlopen(asset["url"], timeout=120) as response:
        archive.write_bytes(response.read())
    if hashlib.sha256(archive.read_bytes()).hexdigest() != asset["sha256"]:
        archive.unlink()
        raise RuntimeError("Checksum mismatch: " + asset["name"])
    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as source:
            source.extract(program.name, folder)
    else:
        with tarfile.open(archive) as source:
            source.extract(program.name, folder)
        program.chmod(0o755)
    archive.unlink()
    return program


def registry_login(program):
    """GitHub passes its token; a developer's machine reads published packages anonymously."""
    token = os.environ.get("REGISTRY_TOKEN")
    if token:
        subprocess.run(
            [
                str(program),
                "login",
                "ghcr.io",
                "--username",
                os.environ["REGISTRY_USER"],
                "--password-stdin",
            ],
            input=token,
            text=True,
            check=True,
            capture_output=True,
        )


def installed():
    """What C:\\papo-ci holds: its tag, and whether it was built here or unpacked as published."""
    try:
        return json.loads((TREE / "toolchain.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def installed_tag():
    return installed().get("tag")


def recipe():
    sys.path.insert(0, str(ROOT / "dist/windows"))
    import toolchain

    return toolchain


def build_tree(arch, tag):
    print(f"Building the Windows toolchain {tag}; later runs reuse it", flush=True)
    recipe().build(arch)
    record = {"tag": tag, "built": "here"}
    (TREE / "toolchain.json").write_text(json.dumps(record) + "\n", encoding="utf-8")


def require_windows():
    if os.name != "nt":
        raise SystemExit("The Windows toolchain is used on Windows; Linux hands jobs to the VM")


def ensure_toolchain(arch, pull=True):
    """The published tree when there is one, else a local build of the same inputs.

    A local build has the same tools but not the same bytes as the tree GitHub runs, so a
    published tag replaces it; without the registry, the local tree is used as it is.
    """
    require_windows()
    tag = toolchain_tag(arch)
    current = installed()
    if current.get("tag") == tag and current.get("built") != "here":
        return tag
    program = oras()
    registry_login(program)
    with tempfile.TemporaryDirectory(dir=ROOT / "build") as folder:
        pulled = pull and subprocess.run(
            [str(program), "pull", tag, "--output", folder],
            capture_output=current.get("tag") == tag,
        )
        if pulled and pulled.returncode == 0:
            print(f"Unpacking {tag}", flush=True)
            recipe().unpack(Path(folder) / ARCHIVE)
            if installed_tag() != tag:
                raise RuntimeError(f"{tag} does not hold the toolchain it names")
            return tag
    if current.get("tag") == tag:
        print(f"Using the local build of {tag}; it is not published yet", flush=True)
        return tag
    build_tree(arch, tag)
    return tag


def publish_toolchain(arch):
    """Build and push only a tag the registry lacks; an existing tag is never replaced."""
    require_windows()
    tag = toolchain_tag(arch)
    program = oras()
    registry_login(program)
    exists = subprocess.run([str(program), "manifest", "fetch", tag], capture_output=True)
    if exists.returncode == 0:
        print(f"Already published: {tag}", flush=True)
        return tag
    if installed_tag() != tag:
        build_tree(arch, tag)
    # These bytes become the published tree, so this machine keeps using them as such.
    record = {"tag": tag, "built": "published"}
    (TREE / "toolchain.json").write_text(json.dumps(record) + "\n", encoding="utf-8")
    with tempfile.TemporaryDirectory(dir=ROOT / "build") as folder:
        archive = Path(folder) / ARCHIVE
        recipe().pack(TREE, archive)
        subprocess.run(
            [
                str(program),
                "push",
                tag,
                f"{ARCHIVE}:{MEDIA_TYPE}",
                "--annotation",
                "org.opencontainers.image.source=https://github.com/prodesert22/papo-tui",
            ],
            cwd=folder,
            check=True,
        )
    return tag


def prepare_tree():
    """The archive leaves out MSYS2's temporary, home and cache folders. They are made empty
    here: the shell points TMP at msys64/tmp, so without it every temporary file fails."""
    for name in ("msys64/tmp", "msys64/home", "msys64/var/cache"):
        (TREE / name).mkdir(parents=True, exist_ok=True)


def link_source():
    """C:\\papo-ci\\src is a junction to this checkout, so every host compiles the same paths."""
    if SOURCE.exists() or SOURCE.is_symlink():
        if SOURCE.resolve() == ROOT.resolve():
            return
        os.rmdir(SOURCE)
    subprocess.run(
        ["cmd", "/c", "mklink", "/J", str(SOURCE), str(ROOT)], check=True, capture_output=True
    )


def run_windows(arch, stage, extra=(), fast=False, environment=None):
    """One stage of the Windows job, on this Windows host."""
    from ci import ENVIRONMENT, RUNNER_CPUS

    tag = ensure_toolchain(arch)
    prepare_tree()
    link_source()
    toolchain = recipe()
    env = toolchain.shell_environment(arch)
    env.update(ENVIRONMENT, PAPO_TOOLCHAIN=tag, PAPO_TOOLCHAIN_DIGEST="")
    if environment:
        env.update(environment)
    cpus = os.cpu_count() if fast else RUNNER_CPUS
    build = f"build/toolchain/windows-{arch}"
    command = f'python dist/build.py --build {build} --prefix "{PREFIX}" --variants release'
    command += f" --jobs {cpus} --test-jobs {cpus}"
    command += " --build-only" if stage == "build" else " --test-only"
    if stage == "test" and extra:
        command += " " + " ".join(_quote(item) for item in extra)
    try:
        toolchain.bash(arch, command, SOURCE, env=env)
    except subprocess.CalledProcessError as error:
        return error.returncode or 1
    return 0


def run_script(arch, script, environment=None):
    """A bash script from the checkout, in the toolchain's shell; the release packaging uses it."""
    from ci import ENVIRONMENT

    ensure_toolchain(arch)
    prepare_tree()
    link_source()
    toolchain = recipe()
    env = toolchain.shell_environment(arch)
    env.update(ENVIRONMENT, PAPO_PREFIX=PREFIX, PAPO_BUILD=f"build/toolchain/windows-{arch}")
    if environment:
        env.update(environment)
    try:
        toolchain.bash(arch, "bash " + _quote(Path(script).as_posix()), SOURCE, env=env)
    except subprocess.CalledProcessError as error:
        return error.returncode or 1
    return 0


def _quote(item):
    return "'" + item.replace("'", "'\\''") + "'"


def vm_folder():
    """The shared folder of the VM described in BUILD.md, beside the main checkout."""
    if os.environ.get("PAPO_WINDOWS_VM"):
        return Path(os.environ["PAPO_WINDOWS_VM"]) / "shared"
    common = subprocess.check_output(
        ["git", "-C", str(ROOT), "rev-parse", "--path-format=absolute", "--git-common-dir"],
        text=True,
    ).strip()
    return Path(common).parent / "build/windows-vm/shared"


def tracked_files(root=ROOT):
    """What a commit of the checkout would hold: tracked and untracked, never ignored, files."""
    listing = subprocess.check_output(
        ["git", "-C", str(root), "ls-files", "-z", "--cached", "--others", "--exclude-standard"]
    )
    return [name for name in listing.decode().split("\0") if name and (root / name).is_file()]


def run_in_vm(arch, stages, extra=(), fast=False):
    """Hand the job to the Windows VM: copy the files, queue a job, follow its log."""
    if arch != "x86_64":
        raise SystemExit("The Windows VM runs x86_64; Windows ARM64 runs only on GitHub")
    shared = vm_folder()
    if not (shared / "jobs").is_dir():
        raise SystemExit(f"No Windows VM shared folder at {shared}; see BUILD.md")
    job = f"ci-{int(time.time())}"
    work = shared / "ci" / job
    for name in tracked_files():
        target = work / "src" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, target)
        shutil.copymode(ROOT / name, target)
    arguments = " ".join(
        _quote(item) for item in ["--stage", stages, *(["--fast"] if fast else [])]
    )
    if extra:
        arguments += " -- " + " ".join(_quote(item) for item in extra)
    script = f"""#!/usr/bin/bash
# Queued by tools/ci.py on the host; runs the Windows job in the VM's own copy of the files.
set -u
share=//host.lan/Data/ci/{job}
rm -rf /c/papo-ci-work/{job} && mkdir -p /c/papo-ci-work
cp -r "$share/src" /c/papo-ci-work/{job}
cd /c/papo-ci-work/{job}
# The share reports times in another time zone, hours ahead of the VM's clock; files from the
# future would make ninja regenerate its build forever.
find . -exec touch -c {{}} +
git init -q && git add -A >/dev/null 2>&1 || true
/c/Python313/python.exe tools/ci.py run windows-release {arguments} > "$share/job.log" 2>&1
status=$?
# The reports GitHub uploads as artifacts: logs, JUnit results and failed cases' diagnostics.
build=build/toolchain/windows-{arch}
mkdir -p "$share/results"
cp -r "$build/ci-logs" "$share/results/" 2>/dev/null
cp -r "$build/release/ci-logs" "$share/results/release-ci-logs" 2>/dev/null
cp -r "$build/release/Testing/Temporary" "$share/results/Testing" 2>/dev/null
echo $status > "$share/exit"
"""
    # The VM moves job.sh away when it starts it; a new file each time avoids a cached copy.
    pending = shared / "jobs" / "job.sh.part"
    pending.write_text(script, encoding="utf-8", newline="\n")
    pending.replace(shared / "jobs" / "job.sh")
    # Windows keeps a lease on the shared folder's listing, and files written by the host do not
    # break it: closing the share makes the VM reconnect and see the job.
    subprocess.run(
        ["docker", "exec", VM_CONTAINER, "smbcontrol", "smbd", "close-share", "Data"],
        capture_output=True,
    )
    print(f"Queued {job} for the Windows VM; log: {work / 'job.log'}", flush=True)
    shown = 0
    while not (work / "exit").exists():
        time.sleep(VM_POLL_SECONDS)
        log = work / "job.log"
        if log.exists():
            text = log.read_text(encoding="utf-8", errors="replace")
            print(text[shown:], end="", flush=True)
            shown = len(text)
    log = work / "job.log"
    if log.exists():
        print(log.read_text(encoding="utf-8", errors="replace")[shown:], end="", flush=True)
    print(f"Reports: {work / 'results'}", flush=True)
    return int((work / "exit").read_text().strip() or 1)
