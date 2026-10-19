"""The native matrix and final gate are behavioral contracts, including cancelled jobs."""

from itertools import product
import os
from pathlib import Path
import subprocess
import sys
import unittest
from ruamel.yaml import YAML

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from ci_timings import report  # noqa: E402

WORKFLOW = YAML(typ="safe").load((ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8"))


class WorkflowTests(unittest.TestCase):
    def test_every_native_platform_is_required_and_variants_are_isolated(self):
        matrix = WORKFLOW["jobs"]["build"]["strategy"]["matrix"]["include"]
        self.assertEqual(
            {(row["platform"], row["arch"]) for row in matrix},
            set(product(("linux", "windows"), ("x86_64", "aarch64"))),
        )
        self.assertFalse(WORKFLOW["jobs"]["build"]["strategy"]["fail-fast"])
        self.assertEqual(
            WORKFLOW["jobs"]["instrumentation"]["strategy"]["matrix"]["variant"],
            ["sanitizers", "coverage"],
        )
        self.assertEqual(
            set(WORKFLOW["jobs"]["required"]["needs"]), {"quality", "build", "instrumentation"}
        )

    def test_final_gate_rejects_failure_cancellation_and_unexpected_skips(self):
        gate = WORKFLOW["jobs"]["required"]["steps"][0]["run"]
        for statuses in product(("success", "failure", "cancelled", "skipped"), repeat=3):
            env = dict(
                os.environ,
                QUALITY=statuses[0],
                BUILD=statuses[1],
                INSTRUMENTATION=statuses[2],
                RELEASE="false",
            )
            result = subprocess.run(
                ["bash", "-e", "-c", gate], env=env, capture_output=True, timeout=3
            )
            self.assertEqual(result.returncode == 0, statuses == ("success",) * 3, statuses)
        env = dict(
            os.environ,
            QUALITY="success",
            BUILD="success",
            INSTRUMENTATION="skipped",
            RELEASE="true",
        )
        self.assertEqual(
            subprocess.run(
                ["bash", "-e", "-c", gate], env=env, capture_output=True, timeout=3
            ).returncode,
            0,
        )

    def test_checkouts_pin_the_event_commit_for_reproducible_reruns(self):
        for job in WORKFLOW["jobs"].values():
            for step in job.get("steps", []):
                if step.get("uses", "").startswith("actions/checkout@"):
                    self.assertEqual(step["with"]["ref"], "${{ inputs.ref || github.sha }}")

    def test_verified_dependencies_are_saved_between_build_and_test(self):
        for job, stage in (
            ("build", "Linux Release"),
            ("build", "Windows"),
            ("instrumentation", "isolated variant"),
        ):
            steps = WORKFLOW["jobs"][job]["steps"]
            start = next(i for i, step in enumerate(steps) if step.get("name") == "Build " + stage)
            end = next(i for i, step in enumerate(steps) if step.get("name") == "Test " + stage)
            self.assertIn("--build-only", steps[start]["run"])
            self.assertIn("--test-only", steps[end]["run"])
            self.assertTrue(
                any(step.get("uses") == "actions/cache/save@v4" for step in steps[start + 1 : end])
            )

    def test_matrix_report_distinguishes_elapsed_and_summed_job_time(self):
        run = dict(
            id=1,
            run_attempt=2,
            head_sha="fixture",
            status="completed",
            conclusion="failure",
            html_url="https://example.invalid/run",
            run_started_at="2026-01-01T00:00:00Z",
        )
        jobs = [
            dict(
                name="linux",
                conclusion="success",
                started_at="2026-01-01T00:00:00Z",
                completed_at="2026-01-01T00:02:00Z",
            ),
            dict(
                name="windows",
                conclusion="failure",
                started_at="2026-01-01T00:00:00Z",
                completed_at="2026-01-01T00:03:00Z",
            ),
        ]
        data = report(run, jobs)
        self.assertEqual(data["elapsed_seconds"], 180)
        self.assertEqual(data["summed_job_seconds"], 300)
        self.assertEqual(data["conclusion"], "failure")


if __name__ == "__main__":
    unittest.main()
