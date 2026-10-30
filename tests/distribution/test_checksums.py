"""Verify manual artifact manifests and their integration with release validation."""

import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "dist"))
import checksums  # noqa: E402
import release  # noqa: E402


class ChecksumsTests(unittest.TestCase):
    def test_generation_is_sorted_repeatable_and_compatible_with_sha256sum(self):
        with tempfile.TemporaryDirectory(prefix="papo checksums ") as directory:
            root = Path(directory)
            (root / "z pacote.zip").write_bytes(b"package contents\0")
            (root / "a-package.deb").write_bytes(b"other package")
            (root / "intermediate").mkdir()
            (root / "intermediate/ignored").write_bytes(b"not a release artifact")
            self.assertEqual(checksums.write_manifest(root), 2)
            manifest = root / "SHA256SUMS"
            original = manifest.read_bytes()
            self.assertEqual(
                original.decode(),
                "".join(
                    f"{hashlib.sha256((root / name).read_bytes()).hexdigest()}  {name}\n"
                    for name in ("a-package.deb", "z pacote.zip")
                ),
            )
            self.assertEqual(checksums.write_manifest(root), 2)
            self.assertEqual(manifest.read_bytes(), original)
            self.assertEqual(checksums.check_manifest(root), 2)
            if shutil.which("sha256sum"):
                subprocess.run(
                    ["sha256sum", "--check", "SHA256SUMS"],
                    cwd=root,
                    check=True,
                    capture_output=True,
                )

    def test_cli_detects_modified_missing_and_extra_files_without_rewriting(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "package.zip"
            package.write_bytes(b"original")
            command = [sys.executable, str(ROOT / "dist/checksums.py"), str(root)]
            subprocess.run(command, check=True, capture_output=True)
            original = (root / "SHA256SUMS").read_bytes()
            subprocess.run([*command, "--check"], check=True, capture_output=True)
            package.write_bytes(b"corrupted")
            corrupted = subprocess.run([*command, "--check"], capture_output=True)
            self.assertNotEqual(corrupted.returncode, 0)
            self.assertIn(b"Checksum mismatch", corrupted.stderr)
            package.unlink()
            missing = subprocess.run([*command, "--check"], capture_output=True)
            self.assertNotEqual(missing.returncode, 0)
            package.write_bytes(b"original")
            (root / "unexpected.zip").write_bytes(b"extra")
            extra = subprocess.run([*command, "--check"], capture_output=True)
            self.assertNotEqual(extra.returncode, 0)
            self.assertIn(b"extra=", extra.stderr)
            self.assertEqual((root / "SHA256SUMS").read_bytes(), original)

    def test_invalid_manifests_reject_duplicates_and_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "package.zip").write_bytes(b"contents")
            digest = checksums.sha256(root / "package.zip")
            for contents in (
                "not a manifest\n",
                f"{digest}  ../package.zip\n",
                f"{digest}  /package.zip\n",
                f"{digest}  package.zip\n{digest}  package.zip\n",
                "",
            ):
                with self.subTest(contents=contents):
                    (root / "SHA256SUMS").write_text(contents, encoding="utf-8")
                    with self.assertRaises(ValueError):
                        checksums.check_manifest(root)

    def test_empty_or_ambiguous_artifacts_preserve_existing_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = root / "SHA256SUMS"
            manifest.write_bytes(b"keep this existing manifest")
            with self.assertRaises(ValueError):
                checksums.write_manifest(root)
            self.assertEqual(manifest.read_bytes(), b"keep this existing manifest")
            for name in ("bad\nfile", "bad\\file", "bad\rfile"):
                with self.subTest(name=name), self.assertRaises(ValueError):
                    checksums.validate_name(name)

    def test_symbolic_links_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "package.zip").write_bytes(b"original")
            checksums.write_manifest(root)
            try:
                (root / "linked.zip").symlink_to(root / "package.zip")
            except (OSError, NotImplementedError):
                self.skipTest("Creating symbolic links requires platform support/privileges")
            with self.assertRaises(ValueError):
                checksums.write_manifest(root)
            with self.assertRaises(ValueError):
                checksums.check_manifest(root)

    def test_release_validation_is_repeatable_but_still_requires_all_assets(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in release.assets("0.5.0"):
                (root / name).write_bytes(name.encode())
            release.check_assets(root, "0.5.0")
            original = (root / "SHA256SUMS").read_bytes()
            release.check_assets(root, "0.5.0")
            self.assertEqual((root / "SHA256SUMS").read_bytes(), original)
            (root / "unexpected.zip").write_bytes(b"extra")
            with self.assertRaises(ValueError):
                release.check_assets(root, "0.5.0")
            (root / "unexpected.zip").unlink()
            (root / "papo-tui-0.5.0-source.tar.gz").unlink()
            with self.assertRaises(ValueError):
                release.check_assets(root, "0.5.0")
            self.assertEqual((root / "SHA256SUMS").read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
