"""Release behavior, installation contracts and CLI error handling."""

from test_context import main as test_main

from test_context import argument

from pathlib import Path
import os
import subprocess
import tempfile
import unittest

BINARY = Path(argument(0)).resolve()
VERSION = argument(1)
ROOT = Path(__file__).resolve().parents[2]


class ReleaseCLI(unittest.TestCase):
    def run_cli(self, *args, **kwargs):
        return subprocess.run([str(BINARY), *args], capture_output=True, timeout=10, **kwargs)

    def test_version_works_without_tty_from_an_unrelated_directory(self):
        with tempfile.TemporaryDirectory(prefix="papo release á ") as directory:
            result = self.run_cli("--version", cwd=directory)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout.replace(b"\r\n", b"\n"), f"papo-tui {VERSION}\n".encode())
        self.assertEqual(result.stderr, b"")

    def test_help_works_without_credentials_locale_or_tty(self):
        env = dict(os.environ, LC_ALL="invalid_papo_locale", PAPO_PASSWORD="NeverPrintThis!")
        result = self.run_cli("--help", env=env)
        self.assertEqual(result.returncode, 0)
        self.assertIn(b"--profiles", result.stdout)
        self.assertNotIn(b"NeverPrintThis", result.stdout + result.stderr)

    def test_help_details_options_environment_and_files_without_shortcuts(self):
        sections = {
            "pt": ("Uso:", "Opções:", "Variáveis de ambiente:", "Arquivos:", "Guia de uso:"),
            "en": ("Usage:", "Options:", "Environment:", "Files:", "User guide:"),
        }
        for language, headings in sections.items():
            with self.subTest(language=language):
                result = self.run_cli("--help", env=dict(os.environ, PAPO_LANGUAGE=language))
                self.assertEqual(result.returncode, 0)
                self.assertEqual(result.stderr, b"")
                text = result.stdout.decode("utf-8").replace("\r\n", "\n")
                self.assertTrue(text.startswith(f"papo-tui {VERSION}\n"))
                for expected in headings + (
                    "--profiles",
                    "--allow-local-http",
                    "--version",
                    "PAPO_LANGUAGE",
                    "CURL_CA_BUNDLE",
                ):
                    self.assertIn(expected, text)
                # Keyboard controls belong to the in-app help and the guide.
                self.assertNotIn("Ctrl+", text)
                self.assertLessEqual(max(len(line) for line in text.splitlines()), 80)

    def test_unknown_or_repeated_arguments_are_rejected_without_echoing_secrets(self):
        cases = [
            ("--profiles", "--profiles"),
            ("--allow-local-http", "--allow-local-http"),
            ("--help", "--profiles"),
            ("NeverPrintThis!",),
        ]
        for args in cases:
            with self.subTest(args=args):
                result = self.run_cli(*args)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b"--help", result.stderr)
                self.assertNotIn(b"NeverPrintThis", result.stdout + result.stderr)

    def test_release_notes_and_changelog_are_present_and_linked(self):
        notes = ROOT / "releases" / f"v{VERSION.removesuffix('-dev')}.md"
        self.assertTrue(notes.is_file())
        self.assertIn(f"releases/{notes.name}", (ROOT / "CHANGELOG.md").read_text(encoding="utf-8"))


if __name__ == "__main__":
    test_main(
        2,
    )
