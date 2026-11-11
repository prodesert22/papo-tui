"""Choosing tests from changed files: rules, reachability and unknown impact."""

import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import test_selection  # noqa: E402
from test_selection import ALL, glob_regex, load_rules, rule_for, select  # noqa: E402

TESTS = {
    "voice-unit": ({"src/net/voice/client.c"}, {"unit", "area:voice"}),
    "tui-voice": ({"src/main.c", "src/net/voice/client.c", "src/ui/draw.c"}, {"area:voice"}),
    "tui-text": ({"src/main.c", "src/net/voice/client.c", "src/ui/draw.c"}, {"area:text"}),
    "auth-probe": ({"src/net/auth/session.c", "tests/probe.c"}, {"area:auth"}),
    "contract": ({"tests/contract/test_contract.py"}, {"area:contract"}),
}


def rules(*entries):
    return [(pattern, glob_regex(pattern), areas) for pattern, areas in entries]


class GlobTests(unittest.TestCase):
    def test_double_star_spans_folders_and_single_star_stays_in_one(self):
        self.assertTrue(glob_regex("src/net/voice/**").match("src/net/voice/a/b.c"))
        self.assertTrue(glob_regex("docs/**").match("docs/usage.md"))
        self.assertTrue(
            glob_regex("src/platform/*/notifications.c").match("src/platform/linux/notifications.c")
        )
        self.assertFalse(
            glob_regex("src/platform/*/notifications.c").match(
                "src/platform/linux/deep/notifications.c"
            )
        )
        self.assertFalse(glob_regex("src/config/audio.*").match("src/config/audio_x.c"))

    def test_first_matching_rule_wins(self):
        table = rules(("src/a/special.c", "*"), ("src/a/**", ["voice"]))
        self.assertEqual(rule_for("src/a/special.c", table), ("src/a/special.c", "*"))
        self.assertEqual(rule_for("src/a/other.c", table), ("src/a/**", ["voice"]))
        self.assertIsNone(rule_for("src/b.c", table))

    def test_repository_rules_name_only_declared_areas(self):
        areas, table = load_rules()
        self.assertIn("voice", areas)
        self.assertTrue(table)


class SelectTests(unittest.TestCase):
    def test_feature_rule_keeps_only_tests_of_that_area_that_use_the_file(self):
        table = rules(("src/net/voice/**", ["voice"]))
        selected, _ = select(["src/net/voice/client.c"], TESTS, {}, table)
        self.assertEqual(selected, {"voice-unit", "tui-voice"})

    def test_shared_file_selects_every_test_that_uses_it_and_nothing_else(self):
        selected, reasons = select(["src/ui/draw.c"], TESTS, {}, [])
        self.assertEqual(selected, {"tui-voice", "tui-text"})
        self.assertIn("2 tests that use it", reasons[0][1])

    def test_header_reaches_tests_through_its_includers(self):
        dependents = {"src/net/auth/session.h": {"src/net/auth/session.c"}}
        selected, _ = select(["src/net/auth/session.h"], TESTS, dependents, [])
        self.assertEqual(selected, {"auth-probe"})

    def test_documentation_needs_no_tests(self):
        selected, reasons = select(["docs/usage.md"], TESTS, {}, rules(("docs/**", [])))
        self.assertEqual(selected, set())
        self.assertIn("no tests", reasons[0][1])

    def test_rule_without_users_selects_the_whole_area(self):
        selected, _ = select(["openapi.yml"], TESTS, {}, rules(("openapi.yml", ["contract"])))
        self.assertEqual(selected, {"contract"})

    def test_build_wide_and_unknown_files_run_everything(self):
        self.assertEqual(select(["cmake/x.cmake"], TESTS, {}, rules(("cmake/**", "*")))[0], ALL)
        self.assertEqual(select(["data/new.bin"], TESTS, {}, [])[0], ALL)

    def test_changes_combine(self):
        table = rules(("docs/**", []), ("src/net/voice/**", ["voice"]))
        selected, _ = select(
            ["docs/usage.md", "src/net/voice/client.c", "tests/probe.c"], TESTS, {}, table
        )
        self.assertEqual(selected, {"voice-unit", "tui-voice", "auth-probe"})


class GraphTests(unittest.TestCase):
    def test_includes_are_followed_transitively_from_the_including_folder(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "src/a").mkdir(parents=True)
            (root / "tests").mkdir()
            (root / "src/a/base.h").write_text("int base;\n")
            (root / "src/a/mid.h").write_text('#include "base.h"\n')
            (root / "src/a/use.c").write_text('#include "a/mid.h"\n')
            dependents = test_selection.include_dependents(root, [str(root / "src")])
            self.assertEqual(dependents["src/a/base.h"], {"src/a/mid.h", "src/a/use.c"})

    def test_python_imports_resolve_support_modules_and_ignore_the_rest(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "tests/support").mkdir(parents=True)
            (root / "tests/terminal").mkdir(parents=True)
            (root / "tests/support/helper.py").write_text("import json\nimport deeper\n")
            (root / "tests/support/deeper.py").write_text("")
            script = root / "tests/terminal/test_case.py"
            script.write_text("import unittest\nfrom helper import thing\n")
            found = test_selection.python_imports(script.resolve(), root.resolve())
            self.assertEqual(
                found,
                {
                    "tests/terminal/test_case.py",
                    "tests/support/helper.py",
                    "tests/support/deeper.py",
                },
            )

    def test_areas_file_is_valid_json_with_descriptions(self):
        data = json.loads((ROOT / "tests/areas.json").read_text(encoding="utf-8"))
        self.assertTrue(all(isinstance(text, str) and text for text in data["areas"].values()))


if __name__ == "__main__":
    unittest.main()
