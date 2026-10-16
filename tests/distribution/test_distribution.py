"""Distribution invariants: reject incomplete releases, unsafe archives and bad locks."""

import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "dist"))
import bootstrap  # noqa: E402 - path and test arguments are prepared above
import package  # noqa: E402 - path and test arguments are prepared above
import release  # noqa: E402 - path and test arguments are prepared above

spec = importlib.util.spec_from_file_location("flatpak_manifest", ROOT / "dist/flatpak/manifest.py")
flatpak = importlib.util.module_from_spec(spec)
spec.loader.exec_module(flatpak)


class DistributionTests(unittest.TestCase):
    def test_checksums_and_unique_sources(self):
        lock = json.loads((ROOT / "dist/dependencies.json").read_text())
        self.assertEqual(len(lock["sources"]), len({x["name"] for x in lock["sources"]}))
        for entry in lock["sources"]:
            self.assertRegex(entry["sha256"], r"^[a-f0-9]{64}$")
            self.assertTrue(entry["url"].startswith("https://"))
            self.assertNotIn("latest", entry["url"])
            self.assertNotIn("continuous", entry["url"])

    def test_flatpak_uses_locked_sources_and_restricted_permissions(self):
        manifest = flatpak.manifest({"path": "/tmp/source.tar.gz"}, "a" * 64)
        names = {x["name"] for x in manifest["modules"]}
        self.assertTrue({"papo-tui", "mpv", "ffmpeg", "libsecret", "emoji-font"} <= names)
        self.assertNotIn("--filesystem=home", manifest["finish-args"])
        self.assertNotIn("--filesystem=host", manifest["finish-args"])
        self.assertEqual(manifest["command"], "papo-tui")
        ffmpeg = next(x for x in manifest["modules"] if x["name"] == "ffmpeg")
        self.assertIn("--disable-gpl", ffmpeg["config-opts"])
        self.assertIn("--disable-nonfree", ffmpeg["config-opts"])

    def test_archive_rejects_traversal_and_external_symlinks(self):
        for name, link in [
            ("../escaped", None),
            ("/absolute", None),
            ("source/link", "../../escaped"),
        ]:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                archive = root / "source.tar"
                with tarfile.open(archive, "w") as output:
                    member = tarfile.TarInfo(name)
                    if link:
                        member.type = tarfile.SYMTYPE
                        member.linkname = link
                    output.addfile(member, io.BytesIO(b""))
                with self.assertRaises(RuntimeError):
                    bootstrap.unpack(archive, root / "out")
                self.assertFalse((root / "escaped").exists())

    def test_bad_checksum_does_not_replace_a_cached_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = root / "original"
            original.write_bytes(b"upstream")
            target = root / "target"
            target.write_bytes(b"existing")
            with self.assertRaises(RuntimeError):
                bootstrap.download(original.as_uri(), "0" * 64, target)
            self.assertEqual(target.read_bytes(), b"existing")
            self.assertFalse((root / "target.part").exists())

    def test_semver_and_incomplete_assets_fail_closed(self):
        self.assertEqual(release.validate_tag("v0.1.0"), "0.1.0")
        for tag in ("0.1.0", "v01.1.0", "v0.1", "v0.1.0-beta", "v0.1.1"):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                release.validate_tag(tag)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(ValueError):
                release.check_assets(root, "0.1.0")
            self.assertFalse((root / "SHA256SUMS").exists())
            for name in release.assets("0.1.0"):
                (root / name).write_bytes(name.encode())
            release.check_assets(root, "0.1.0")
            for line in (root / "SHA256SUMS").read_text().splitlines():
                digest, name = line.split("  ", 1)
                self.assertEqual(digest, hashlib.sha256((root / name).read_bytes()).hexdigest())

    def test_machine_detection_rejects_wrong_architecture(self):
        import struct

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "binary"
            header = bytearray(64)
            header[:6] = b"\x7fELF\x02\x01"
            struct.pack_into("<H", header, 18, 183)
            path.write_bytes(header)
            self.assertEqual(package.binary_arch(path), "aarch64")
            path.write_bytes(b"not a binary")
            self.assertIsNone(package.binary_arch(path))


if __name__ == "__main__":
    unittest.main()
