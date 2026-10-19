"""Regression tests for discovery, isolation, reports and synthetic server failure propagation."""

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/support"))
sys.path.insert(0, str(ROOT / "tools"))
from mock_server import MockServer  # noqa: E402
from run_case import allowed_skip_reason, diagnostics, discover, redact  # noqa: E402
from test_report import compare, read_results  # noqa: E402


class HarnessTests(unittest.TestCase):
    def test_discovery_does_not_execute_imports_and_names_survive_insertion(self):
        with tempfile.TemporaryDirectory() as folder:
            script = Path(folder, "case.py")
            script.write_text(
                "raise RuntimeError('must not import')\nclass Cases:\n    def test_b(self): pass\n",
                encoding="utf-8",
            )
            self.assertEqual(discover(script), ["Cases.test_b"])
            script.write_text(
                script.read_text(encoding="utf-8") + "    def test_a(self): pass\n",
                encoding="utf-8",
            )
            self.assertEqual(discover(script), ["Cases.test_a", "Cases.test_b"])

    def test_graphics_discovery_covers_each_protocol_and_panel_exactly_once(self):
        script = ROOT / "tests/terminal/test_media_terminal.py"
        cases = [case for case in discover(script) if "floating_panels" in case]
        self.assertEqual(len(cases), 33)
        self.assertEqual(len(cases), len(set(cases)))
        for protocol in ("kitty", "sixel", "iterm2"):
            self.assertEqual(sum("[" + protocol + "," in case for case in cases), 11)

    def test_handler_assertion_fails_main_thread_and_server_stops(self):
        server = MockServer()

        def fail(*_):
            raise AssertionError("controlled handler failure")

        server.api_handler = fail
        with self.assertRaisesRegex(AssertionError, "controlled handler failure"):
            with server:
                try:
                    urllib.request.urlopen(server.url + "/server", timeout=2)
                except OSError:
                    pass
        self.assertFalse(server.thread.is_alive())
        self.assertFalse(server.server.handlers)

    def test_shutdown_closes_the_listener_and_joins_the_server(self):
        server = MockServer()
        with server:
            pass
        self.assertFalse(server.thread.is_alive())
        self.assertEqual(server.server.fileno(), -1)

    def test_importing_terminal_support_preserves_argv(self):
        source = ROOT / "tests/support/terminal_support.py"
        spec = importlib.util.spec_from_file_location("isolated_terminal_support", source)
        module = importlib.util.module_from_spec(spec)
        with patch.object(sys, "argv", ["runner", "one", "two"]):
            spec.loader.exec_module(module)
            self.assertEqual(sys.argv, ["runner", "one", "two"])

    def test_runner_returns_failure_for_missing_or_failed_case_and_writes_report(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            script = directory / "fixture.py"
            script.write_text(
                "import unittest\nclass Cases(unittest.TestCase):\n    def test_failure(self): self.fail('controlled failure')\n",
                encoding="utf-8",
            )
            env = dict(os.environ, PAPO_TEST_REPORTS=str(directory / "results"))
            for name in ("Cases.test_failure", "Cases.test_missing"):
                result = subprocess.run(
                    [
                        sys.executable,
                        str(ROOT / "tests/support/run_case.py"),
                        "--script",
                        str(script),
                        "--case",
                        name,
                        "--",
                    ],
                    env=env,
                    capture_output=True,
                    timeout=5,
                )
                self.assertEqual(result.returncode, 1)
                report = json.loads(
                    (directory / "results" / ("fixture." + name) / "result.json").read_text(
                        encoding="utf-8"
                    )
                )
                self.assertFalse(report["success"])

    def test_runner_preserves_explicit_skip_and_already_closed_terminal(self):
        with tempfile.TemporaryDirectory() as folder:
            script = Path(folder, "fixture.py")
            script.write_text(
                "import unittest\nfrom types import SimpleNamespace\nimport test_context\n"
                "class Cases(unittest.TestCase):\n"
                "    @unittest.skip('explicit platform exclusion')\n"
                "    def test_skip(self): pass\n"
                "    def test_closed(self):\n"
                "        test_context.terminals.append(SimpleNamespace(_closed=True))\n",
                encoding="utf-8",
            )
            env = dict(os.environ, PAPO_TEST_REPORTS=str(Path(folder, "reports")))
            for case, expected, required in (
                ("Cases.test_skip", 77, "0"),
                ("Cases.test_skip", 1, "1"),
                ("Cases.test_closed", 0, "1"),
            ):
                result = subprocess.run(
                    [
                        sys.executable,
                        str(ROOT / "tests/support/run_case.py"),
                        "--script",
                        str(script),
                        "--case",
                        case,
                        "--",
                    ],
                    env=dict(env, PAPO_TEST_REQUIRED=required),
                    capture_output=True,
                    timeout=5,
                )
                self.assertEqual(result.returncode, expected, result.stderr)

    def test_required_platform_skips_are_declared_for_exact_cases(self):
        script = ROOT / "tests/terminal/test_members.py"
        case = "Members.test_header_icons_remain_visible_at_narrow_sizes"
        self.assertIn("ncurses", allowed_skip_reason(script, case, "windows"))
        self.assertIsNone(allowed_skip_reason(script, case, "linux"))
        self.assertIsNone(allowed_skip_reason(script, "Members.test_missing", "windows"))

    def test_animation_fixture_checks_encoding_without_probing_cli_decoders(self):
        from terminal_emojis import encode_emoji_animation

        with patch(
            "terminal_emojis.subprocess.run", return_value=SimpleNamespace(stdout=b"fixture")
        ) as run:
            self.assertEqual(encode_emoji_animation(b"GIF fixture", "WEBP"), b"fixture")
        run.assert_called_once()
        command = run.call_args.args[0]
        self.assertIn("libwebp_anim", command)
        self.assertNotIn("-decoders", command)

    def test_failure_snapshot_precedes_terminal_cleanup(self):
        with tempfile.TemporaryDirectory() as folder:
            script = Path(folder, "fixture.py")
            script.write_text(
                "import unittest\nfrom types import SimpleNamespace\nimport test_context\n"
                "class Cases(unittest.TestCase):\n"
                "    def test_failure(self):\n"
                "        terminal = SimpleNamespace(screen=SimpleNamespace(display=['visible before cleanup']), output=b'', close=lambda: True)\n"
                "        test_context.terminals.append(terminal)\n"
                "        self.addCleanup(setattr, terminal.screen, 'display', ['cleared'])\n"
                "        self.fail('controlled failure')\n",
                encoding="utf-8",
            )
            reports = Path(folder, "reports")
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tests/support/run_case.py"),
                    "--script",
                    str(script),
                    "--case",
                    "Cases.test_failure",
                    "--",
                ],
                env=dict(os.environ, PAPO_TEST_REPORTS=str(reports)),
                capture_output=True,
                timeout=5,
            )
            self.assertEqual(result.returncode, 1)
            data = json.loads(
                (reports / "fixture.Cases.test_failure/diagnostic.json").read_text(encoding="utf-8")
            )
            self.assertEqual(data["terminals"][0]["screen"], ["visible before cleanup"])

    def test_reports_do_not_count_skips_or_errors_as_passes(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder, "results.xml")
            path.write_text(
                '<testsuite><testcase name="ok" time="1"/><testcase name="skip"><skipped/></testcase><testcase name="broken"><error/></testcase></testsuite>',
                encoding="utf-8",
            )
            self.assertEqual(
                [case["status"] for case in read_results(path)], ["passed", "skipped", "failed"]
            )

    def test_timing_comparison_rejects_newly_skipped_scenarios(self):
        with tempfile.TemporaryDirectory() as folder:
            reports = []
            for status in ("passed", "skipped"):
                path = Path(folder, status + ".json")
                path.write_text(json.dumps({"cases": [{"name": "case", "status": status}]}))
                reports.append(path)
            with self.assertRaisesRegex(ValueError, "completion states"):
                compare(reports)

    def test_diagnostics_keep_the_last_screen_when_finally_already_closed_terminal(self):
        with tempfile.TemporaryDirectory() as folder:
            terminal = SimpleNamespace(
                screen=SimpleNamespace(display=["cleared"]),
                _screen_before_close=["visible before quit"],
                output=b"",
            )
            with patch("test_context.terminals", [terminal]), patch("test_context.servers", []):
                diagnostics(Path(folder))
            data = json.loads(Path(folder, "diagnostic.json").read_text(encoding="utf-8"))
            self.assertEqual(data["terminals"][0]["screen"], ["visible before quit"])

    def test_diagnostics_redact_credentials_and_cookies(self):
        self.assertNotIn("Synthetic!123", redact("Synthetic!123"))
        self.assertNotIn("private-cookie", redact("Auth=private-cookie;"))


if __name__ == "__main__":
    unittest.main()
