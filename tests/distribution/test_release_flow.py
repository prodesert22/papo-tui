"""Exercise the release workflow's real identity script against local Git remotes."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from ruamel.yaml import YAML

ROOT = Path(__file__).resolve().parents[2]
yaml = YAML(typ="safe")
yaml.version = (1, 2)
WORKFLOW = yaml.load((ROOT / ".github/workflows/release.yml").read_text())
IDENTITY = WORKFLOW["jobs"]["identity"]["steps"][-1]["run"]


class ReleaseFlow(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="papo-release-test-")
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.source, self.remote = self.folder / "checkout", self.folder / "remote.git"
        subprocess.run(["git", "init", "--bare", str(self.remote)], check=True, capture_output=True)
        subprocess.run(
            ["git", "init", "-b", "main", str(self.source)], check=True, capture_output=True
        )
        for key, value in (
            ("user.name", "Synthetic release test"),
            ("user.email", "fixture@example.invalid"),
            ("commit.gpgSign", "false"),
            ("tag.gpgSign", "false"),
        ):
            self.git("config", key, value)
        (self.source / "dist").mkdir()
        for name in ("release.py", "bootstrap.py"):
            shutil.copy2(ROOT / "dist" / name, self.source / "dist" / name)
        (self.source / "releases").mkdir()
        self.write_version("0.3.0")
        self.commit("Initial version")
        self.base = self.git("rev-parse", "HEAD")
        self.git("remote", "add", "origin", str(self.remote))
        self.git("push", "-u", "origin", "main")
        self.output = self.folder / "outputs"
        fake_bin = self.folder / "bin"
        fake_bin.mkdir()
        gh = fake_bin / "gh"
        gh.write_text(
            '#!/bin/sh\n[ "$FAKE_RELEASE_STATE" != missing ] || exit 1\n'
            'printf "%s\\n" "$FAKE_RELEASE_STATE"\n'
        )
        gh.chmod(0o755)
        self.env = dict(os.environ, PATH=str(fake_bin) + os.pathsep + os.environ["PATH"])

    def git(self, *args):
        return subprocess.check_output(
            ["git", *args], cwd=self.source, text=True, stderr=subprocess.PIPE
        ).strip()

    def write_version(self, version):
        (self.source / "CMakeLists.txt").write_text(
            f"project(papo_tui VERSION {version} LANGUAGES C)\n"
        )
        (self.source / "CHANGELOG.md").write_text(f"## [{version}]\n")
        (self.source / "releases" / f"v{version}.md").write_text(f"# Version {version}\n")

    def commit(self, title):
        self.git("add", ".")
        self.git("commit", "-m", title)

    def candidate(self):
        self.write_version("0.3.1")
        self.commit("Release 0.3.1: Prepare maintenance release")
        self.git("push", "origin", "main")

    def identity(self, *, event="push", ref="refs/heads/main", state="missing"):
        self.output.unlink(missing_ok=True)
        result = subprocess.run(
            ["bash", "-e", "-o", "pipefail", "-c", IDENTITY],
            cwd=self.source,
            env=dict(
                self.env,
                RELEASE_EVENT=event,
                RELEASE_REF=ref,
                PREVIOUS_SHA=self.base,
                FAKE_RELEASE_STATE=state,
                GITHUB_OUTPUT=str(self.output),
            ),
            capture_output=True,
            text=True,
        )
        output = (
            dict(line.split("=", 1) for line in self.output.read_text().splitlines())
            if self.output.exists()
            else {}
        )
        return result, output

    def remote_tag(self):
        return self.git("ls-remote", "origin", "refs/tags/v0.3.1*")

    def test_release_trigger_only_watches_version_file_pushes_to_main(self):
        self.assertEqual(
            WORKFLOW["on"], {"push": {"branches": ["main"], "paths": ["CMakeLists.txt"]}}
        )

    def test_pull_requests_other_branches_and_manual_runs_cannot_create_tags(self):
        self.candidate()
        for event, ref in (
            ("pull_request", "refs/pull/2/merge"),
            ("pull_request", "refs/heads/main"),
            ("push", "refs/heads/dev"),
            ("push", "refs/tags/v0.3.1"),
            ("workflow_dispatch", "refs/heads/main"),
        ):
            with self.subTest(event=event, ref=ref):
                result, output = self.identity(event=event, ref=ref)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(output, {"enabled": "false"})
                self.assertEqual(self.remote_tag(), "")

    def test_unchanged_version_skips_without_creating_a_tag(self):
        with (self.source / "CMakeLists.txt").open("a") as stream:
            stream.write("# Build change without a version increase\n")
        self.commit("Update build configuration without changing the version")
        self.git("push", "origin", "main")
        result, output = self.identity()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output, {"enabled": "false"})
        self.assertEqual(self.remote_tag(), "")

    def test_merge_tags_exact_main_commit_and_supports_retries(self):
        self.git("switch", "-c", "dev")
        self.write_version("0.3.1")
        self.commit("Release 0.3.1: Prepare maintenance release")
        self.git("switch", "main")
        self.git("merge", "--no-ff", "dev", "-m", "Merge pull request from dev")
        self.git("push", "origin", "main")
        merged = self.git("rev-parse", "HEAD")
        result, output = self.identity()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output, {"enabled": "true", "tag": "v0.3.1", "version": "0.3.1"})
        self.assertEqual(self.git("cat-file", "-t", "v0.3.1"), "tag")
        self.assertEqual(self.git("rev-parse", "v0.3.1^{commit}"), merged)
        self.assertEqual(self.git("rev-parse", "origin/main"), merged)
        original = self.remote_tag()
        for state in ("missing", "true"):
            with self.subTest(state=state):
                result, output = self.identity(state=state)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(output["enabled"], "true")
                self.assertEqual(self.remote_tag(), original)

    def test_invalid_regressive_or_undocumented_versions_cannot_create_tags(self):
        for version in ("0.2.9", "0.3.1-rc1", "0.3.1"):
            with self.subTest(version=version):
                self.write_version(version)
                if version == "0.3.1":
                    (self.source / "releases/v0.3.1.md").unlink()
                self.commit("Prepare synthetic candidate")
                self.git("push", "origin", "main")
                result, output = self.identity()
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn("enabled", output)
                self.assertEqual(self.remote_tag(), "")

    def test_existing_conflicting_tag_is_never_moved(self):
        self.candidate()
        self.git("tag", "-a", "v0.3.1", self.base, "-m", "Conflicting synthetic tag")
        self.git("push", "origin", "refs/tags/v0.3.1")
        original = self.remote_tag()
        for remove_local in (False, True):
            with self.subTest(remove_local=remove_local):
                if remove_local:
                    self.git("tag", "-d", "v0.3.1")
                result, output = self.identity()
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn("enabled", output)
                self.assertEqual(self.remote_tag(), original)

    def test_published_release_stops_before_tag_creation_or_builds(self):
        self.candidate()
        result, output = self.identity(state="false")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Refusing to replace a published release", result.stdout)
        self.assertNotIn("enabled", output)
        self.assertEqual(self.remote_tag(), "")

    def test_unmerged_or_dirty_metadata_cannot_be_tagged(self):
        self.git("switch", "-c", "dev")
        self.write_version("0.3.1")
        self.commit("Prepare unmerged version")
        command = [sys.executable, "dist/release.py", "v0.3.1", "--ensure-tag"]
        result = subprocess.run(command, cwd=self.source, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.remote_tag(), "")
        self.git("push", "origin", "HEAD:main")
        with (self.source / "CHANGELOG.md").open("a") as stream:
            stream.write("Uncommitted metadata\n")
        result = subprocess.run(command, cwd=self.source, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.remote_tag(), "")


if __name__ == "__main__":
    unittest.main()
