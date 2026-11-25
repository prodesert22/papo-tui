"""GitHub and a developer's machine must run the CI jobs with the same tools and commands."""

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from ruamel.yaml import YAML

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import ci  # noqa: E402 - path is prepared above

WORKFLOW = YAML(typ="safe").load((ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8"))
DOCKERFILE = (ROOT / "dist/ci/Dockerfile").read_text(encoding="utf-8")
LINUX_JOBS = ("toolchain", "quality", "instrumentation")


def linux_steps():
    for name in LINUX_JOBS:
        for step in WORKFLOW["jobs"][name]["steps"]:
            if "matrix.platform == 'windows'" not in str(step.get("if", "")):
                yield name, step
    for step in WORKFLOW["jobs"]["build"]["steps"]:
        condition = str(step.get("if", ""))
        if "matrix.platform == 'linux'" in condition and "!inputs.release" in condition:
            yield "build", step


class WorkflowParity(unittest.TestCase):
    def test_linux_ci_installs_nothing_on_the_runner(self):
        """Tools the runner image provides would differ from the developer's."""
        installers = re.compile(r"apt-get|pip install|linux-deps\.sh|setup-python|setup-go|go run")
        for job, step in linux_steps():
            text = str(step.get("run", "")) + str(step.get("uses", ""))
            self.assertIsNone(installers.search(text), (job, step.get("name")))
            self.assertNotIn("actions/cache", text, (job, step.get("name")))

    def test_every_linux_command_is_a_job_the_local_command_knows(self):
        commands = [
            str(step["run"])
            for _, step in linux_steps()
            if "tools/ci.py run" in str(step.get("run", ""))
        ]
        self.assertTrue(commands)
        for command in commands:
            names = re.findall(r"tools/ci\.py run \"?([\w$-]+)", command)
            for name in names:
                name = name.replace("$VARIANT", "sanitizers")
                self.assertIn(name, ci.JOBS, command)
        variants = WORKFLOW["jobs"]["instrumentation"]["strategy"]["matrix"]["variant"]
        self.assertTrue(all("linux-" + variant in ci.JOBS for variant in variants))

    def test_jobs_wait_for_the_published_toolchain_of_every_native_platform(self):
        for name in ("quality", "build", "instrumentation"):
            self.assertEqual(WORKFLOW["jobs"][name]["needs"], "toolchain")
        rows = WORKFLOW["jobs"]["toolchain"]["strategy"]["matrix"]["include"]
        built = {(row["platform"], row["arch"]) for row in rows}
        tested = {
            (row["platform"], row["arch"])
            for row in WORKFLOW["jobs"]["build"]["strategy"]["matrix"]["include"]
        }
        self.assertEqual(built, tested)
        self.assertEqual(
            {arch for platform, arch in built if platform == "linux"}, set(ci.PLATFORMS)
        )
        self.assertEqual(WORKFLOW["jobs"]["toolchain"]["permissions"]["packages"], "write")

    def test_windows_ci_runs_the_local_command_without_msys2_setup_or_caches(self):
        steps = [
            step
            for step in WORKFLOW["jobs"]["build"]["steps"]
            if "matrix.platform == 'windows'" in str(step.get("if", ""))
        ]
        self.assertTrue(steps)
        for step in steps:
            text = str(step.get("run", "")) + str(step.get("uses", ""))
            self.assertNotIn("setup-msys2", text, step.get("name"))
            self.assertNotIn("actions/cache", text, step.get("name"))
            self.assertNotIn("pip install", text, step.get("name"))
            if "run" in step:
                self.assertIn("tools/ci.py", step["run"], step.get("name"))
        for step in steps:
            for name in re.findall(r"tools/ci\.py run ([\w-]+)", str(step.get("run", ""))):
                self.assertIn(name, ci.WINDOWS_JOBS)


class WindowsToolchain(unittest.TestCase):
    def test_the_archive_is_named_after_every_file_the_recipe_reads(self):
        sys.path.insert(0, str(ROOT / "dist/windows"))
        import ci_windows
        import toolchain

        recipe = (ROOT / "dist/windows/toolchain.py").read_text(encoding="utf-8")
        # What it writes under build/ is output, not an input.
        read = [
            name
            for name in re.findall(r'ROOT / "([^"]+)"', recipe)
            if not name.startswith("build/")
        ]
        self.assertIn("dist/windows/toolchain.json", read)
        for name in read:
            self.assertTrue(
                any(
                    name == item or name.startswith(item + "/")
                    for item in ci_windows.TOOLCHAIN_INPUTS
                ),
                name,
            )
        for arch in ("x86_64", "aarch64"):
            self.assertRegex(
                ci_windows.toolchain_tag(arch), rf"^ghcr\.io/.+:windows-{arch}-[0-9a-f]{{16}}$"
            )
            packages = toolchain.packages(arch)
            self.assertIn("flex", packages)
            self.assertIn("bison", packages)
            self.assertFalse(any("{" in name for name in packages))

    def test_downloads_are_pinned(self):
        lock = json.loads((ROOT / "dist/windows/toolchain.json").read_text(encoding="utf-8"))
        pinned = [lock["msys2"], *(system["python"] for system in lock["systems"].values())]
        for item in pinned:
            self.assertRegex(item["sha256"], r"^[0-9a-f]{64}$")
            self.assertTrue(item["url"].startswith("https://"))
        oras = json.loads((ROOT / "dist/tools.json").read_text(encoding="utf-8"))["oras"]
        for asset in oras["assets"]:
            self.assertRegex(asset["sha256"], r"^[0-9a-f]{64}$")

    def test_vm_jobs_carry_only_files_a_commit_would_hold(self):
        sys.path.insert(0, str(ROOT / "tools"))
        import ci_windows

        files = ci_windows.tracked_files()
        self.assertIn("tools/ci.py", files)
        self.assertFalse(any(name.startswith("build/") for name in files))


class ImageInputs(unittest.TestCase):
    def test_the_image_copies_only_files_its_tag_covers(self):
        copied = []
        for line in DOCKERFILE.splitlines():
            match = re.match(r"COPY (?:--\S+ )*(.+) \S+$", line.strip().rstrip("\\").strip())
            if match:
                copied += match.group(1).split()
        joined = "\n".join(DOCKERFILE.splitlines())
        for continuation in re.findall(r"COPY ([^\n]*\\\n[^\n]*)", joined):
            copied += continuation.replace("\\\n", " ").split()[:-1]
        sources = {item for item in copied if "/" in item and not item.startswith("/")}
        self.assertTrue(sources)
        covered = set(ci.IMAGE_INPUTS)
        for source in sources:
            self.assertTrue(
                any(source == name or source.startswith(name + "/") for name in covered), source
            )

    def test_the_builder_receives_exactly_the_hashed_files(self):
        ignore = (ROOT / "dist/ci/Dockerfile.dockerignore").read_text(encoding="utf-8")
        allowed = {line[1:] for line in ignore.splitlines() if line.startswith("!")}
        # The Dockerfile and its ignore file are read by the builder without being sent.
        self.assertEqual(
            allowed,
            set(ci.IMAGE_INPUTS) - {"dist/ci/Dockerfile", "dist/ci/Dockerfile.dockerignore"},
        )

    def test_base_image_and_downloads_are_pinned_by_digest(self):
        base = re.search(r"ARG UBUNTU=(\S+)", DOCKERFILE).group(1)
        self.assertRegex(base, r"@sha256:[0-9a-f]{64}$")
        for line in DOCKERFILE.splitlines():
            if line.startswith("ADD "):
                self.assertRegex(line, r"--checksum=sha256:[0-9a-f]{64}", line)
        self.assertRegex(DOCKERFILE, r"APT_SNAPSHOT=\d{8}T\d{6}Z")
        self.assertIn("Pin-Priority: 1001", DOCKERFILE)

    def test_tag_changes_with_any_input_and_ignores_caches(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ci.IMAGE_INPUTS:
                source = ROOT / name
                target = root / name
                if source.is_dir():
                    shutil.copytree(source, target, ignore=shutil.ignore_patterns("__pycache__"))
                else:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(source, target)
            self.assertEqual(ci.image_hash(root), ci.image_hash(ROOT))
            (root / "dist/patches/__pycache__").mkdir()
            (root / "dist/patches/__pycache__/x.pyc").write_bytes(b"cache")
            self.assertEqual(ci.image_hash(root), ci.image_hash(ROOT))
            (root / "dist/patches/new.patch").write_text("change", encoding="utf-8")
            self.assertNotEqual(ci.image_hash(root), ci.image_hash(ROOT))
        self.assertRegex(ci.image_tag("x86_64"), r"^ghcr\.io/.+:linux-x86_64-[0-9a-f]{16}$")


class ContainerCommand(unittest.TestCase):
    def command(self, **options):
        with patch.object(ci, "git_common_mount", return_value=[]):
            return ci.container_command(
                "linux-release",
                "test",
                "image:tag",
                Path("/checkout"),
                arch="x86_64",
                environment={"GITHUB_ACTIONS": "true", "GH_TOKEN": "secret", "HOME": "/home/x"},
                **options,
            )

    def test_jobs_get_a_runners_limits_and_the_shared_environment(self):
        command = self.command(slot=1)
        joined = " ".join(command)
        with patch.object(ci.os, "cpu_count", return_value=24):
            self.assertEqual(ci.cpu_set(1), "4-7")
        self.assertIn("--memory 16g", joined)
        self.assertTrue("--cpuset-cpus" in command or "--cpus" in command)
        for key, value in ci.ENVIRONMENT.items():
            self.assertIn(f"{key}={value}", command)
        self.assertIn("GITHUB_ACTIONS=true", command)
        self.assertIn("/checkout:/workspace", command)
        self.assertEqual(
            command[command.index("image:tag") + 1 :][:4],
            ["python", "tools/ci.py", "inside", "linux-release"],
        )

    def test_credentials_and_host_paths_stay_out_of_the_container(self):
        joined = " ".join(self.command())
        self.assertNotIn("secret", joined)
        self.assertNotIn("/home/x", joined)

    def test_fast_mode_removes_only_the_limits(self):
        fast = self.command(fast=True)
        self.assertNotIn("--memory", fast)
        self.assertNotIn("--cpuset-cpus", fast)
        self.assertIn("--fast", fast)

    def test_cpu_sets_never_overlap_while_jobs_run_together(self):
        with patch.object(ci.os, "cpu_count", return_value=24):
            sets = [ci.cpu_set(slot) for slot in range(len(ci.JOBS))]
        self.assertEqual(len(set(sets)), len(sets))
        with patch.object(ci.os, "cpu_count", return_value=4):
            self.assertEqual(ci.cpu_set(3), "0-3")
        with patch.object(ci.os, "cpu_count", return_value=2):
            self.assertIsNone(ci.cpu_set(0))

    @unittest.skipUnless(shutil.which("git"), "git is required")
    def test_worktree_objects_are_mounted_read_only(self):
        with tempfile.TemporaryDirectory() as directory:
            main = Path(directory) / "main"
            subprocess.run(["git", "init", "--quiet", str(main)], check=True)
            env = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@example.invalid")
            env.update(GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@example.invalid")
            subprocess.run(
                ["git", "-C", str(main), "commit", "--quiet", "--allow-empty", "-m", "x"],
                check=True,
                env=env,
            )
            tree = Path(directory) / "tree"
            subprocess.run(
                ["git", "-C", str(main), "worktree", "add", "--quiet", str(tree)], check=True
            )
            mount = ci.git_common_mount(tree)
            self.assertEqual(mount[0], "--volume")
            self.assertTrue(mount[1].endswith(":ro"))
            self.assertEqual(ci.git_common_mount(main), [])


if __name__ == "__main__":
    unittest.main()
