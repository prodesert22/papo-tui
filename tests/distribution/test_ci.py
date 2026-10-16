"""Concurrent release orchestration must retain every failure and isolate builds."""

from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "dist"))
import build as ci_build  # noqa: E402
import ci_linux  # noqa: E402


class BuildOrchestration(unittest.TestCase):
    def test_variants_share_preparation_run_concurrently_and_retain_failure(self):
        gate = threading.Barrier(2)
        lock = threading.Lock()
        attempts = []
        active = peak = 0

        def variant(directory, prefix, mode, jobs, test_jobs, log):
            nonlocal active, peak
            with lock:
                active += 1
                peak = max(peak, active)
                attempts.append((directory.name, jobs, test_jobs))
            try:
                if mode != "coverage":
                    gate.wait(timeout=5)
                log.write(mode + "\n")
                if mode == "release":
                    raise subprocess.CalledProcessError(1, ["synthetic compiler"])
            finally:
                with lock:
                    active -= 1

        with (
            tempfile.TemporaryDirectory() as directory,
            patch.object(
                sys,
                "argv",
                [
                    "build.py",
                    "--build",
                    directory,
                    "--prefix",
                    directory + "/deps",
                    "--variants",
                    "release,sanitizers,coverage",
                    "--jobs",
                    "2",
                    "--test-jobs",
                    "4",
                ],
            ),
            patch.object(ci_build.subprocess, "run") as prepare,
            patch.object(ci_build, "build_variant", side_effect=variant),
            redirect_stdout(io.StringIO()),
        ):
            with self.assertRaises(SystemExit) as result:
                ci_build.main()
            self.assertEqual(result.exception.code, 1)
            self.assertEqual(prepare.call_count, 2)  # bootstrap and resources, once each
            self.assertEqual(peak, 2)
            self.assertEqual(
                sorted(attempts), [(name, 1, 2) for name in ("coverage", "release", "sanitizers")]
            )
            self.assertEqual(len(list((Path(directory) / "ci-logs").glob("*.log"))), 3)

    def test_linux_target_failure_still_records_all_targets_and_blocks_assembly(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ci_linux.TARGETS:
                (root / "build/release-tasks" / name / "source").mkdir(parents=True)

            def commands(name, source, architecture, version, jobs):
                code = 9 if name == "fedora" else 0
                return [[sys.executable, "-c", f"print('{name}'); raise SystemExit({code})"]], None

            with (
                patch.object(ci_linux, "ROOT", root),
                patch.object(ci_linux, "prepare"),
                patch.object(ci_linux, "commands", side_effect=commands),
                patch.object(ci_linux, "assemble") as assemble,
                patch.object(
                    sys,
                    "argv",
                    ["ci_linux.py", "--arch", "x86_64", "--version", "0.2.0", "--jobs", "2"],
                ),
                redirect_stdout(io.StringIO()),
            ):
                with self.assertRaises(SystemExit) as result:
                    ci_linux.main()
                self.assertEqual(result.exception.code, 1)
                assemble.assert_not_called()
            logs = root / "build/release-tasks/logs"
            report = json.loads((logs / "timings.json").read_text())
            self.assertEqual({row["target"] for row in report}, set(ci_linux.TARGETS))
            self.assertEqual(sum(row["exit_code"] != 0 for row in report), 1)
            self.assertEqual(
                next(row["exit_code"] for row in report if row["target"] == "fedora"), 9
            )
            for name in ci_linux.TARGETS:
                self.assertIn(name, (logs / (name + ".log")).read_text())


if __name__ == "__main__":
    unittest.main()
