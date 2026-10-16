"""Distribution invariants: reject incomplete releases, unsafe archives and bad locks."""

import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

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
        libplacebo = next(x for x in manifest["modules"] if x["name"] == "libplacebo")
        self.assertEqual(
            libplacebo["build-options"]["env"]["PYTHONPATH"],
            "/app/lib/python3/site-packages",
        )
        python_tools = [
            x for x in manifest["modules"] if x["name"] in ("python-markupsafe", "python-jinja2")
        ]
        self.assertEqual([x["name"] for x in python_tools], ["python-markupsafe", "python-jinja2"])
        self.assertLess(
            [x["name"] for x in manifest["modules"]].index("python-jinja2"),
            [x["name"] for x in manifest["modules"]].index("libplacebo"),
        )
        self.assertIn("/lib/python3/site-packages/jinja2", manifest["cleanup"])
        locked = json.loads((ROOT / "dist/dependencies.json").read_text())["sources"]
        modules = {x["name"]: x for x in manifest["modules"]}
        for entry in locked:
            if "posix" not in entry.get("platforms", ["posix"]):
                self.assertNotIn(entry["name"], modules)
                continue
            self.assertEqual(
                modules[entry["name"]]["sources"][0]["dest-filename"], entry["archive"]
            )
        flatpak_tools = json.loads((ROOT / "dist/flatpak/build-dependencies.json").read_text())[
            "sources"
        ]
        for entry in flatpak_tools:
            self.assertRegex(entry["sha256"], r"^[a-f0-9]{64}$")
            self.assertEqual(
                modules[entry["name"]]["sources"][0]["dest-filename"], entry["archive"]
            )

    def test_windows_libsixel_patch_is_repeatable_and_uses_native_timer(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            (source / "src").mkdir()
            encoder = source / "src/encoder.c"
            encoder.write_text('#include "rgblookup.h"\nvoid encode(void) { nanosleep(0, 0); }\n')
            bootstrap.patch_windows_libsixel(source)
            first = encoder.read_text()
            bootstrap.patch_windows_libsixel(source)
            self.assertEqual(encoder.read_text(), first)
            self.assertIn("CreateWaitableTimer", first)
            self.assertIn("#define nanosleep sixel_windows_nanosleep", first)

    def test_windows_mpv_uses_native_threading(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, work, prefix = (root / name for name in ("source", "work", "prefix"))
            source.mkdir()
            calls = []

            def capture(args, **kwargs):
                calls.append((args, kwargs))
                return bootstrap.subprocess.CompletedProcess(args=args, returncode=0)

            with (
                patch.object(bootstrap.os, "name", "nt"),
                patch.object(bootstrap.subprocess, "run", side_effect=capture),
            ):
                bootstrap.build(
                    {"name": "mpv", "buildsystem": "meson"},
                    source,
                    work,
                    prefix,
                    4,
                    {"PATH": ""},
                )
            setup = next(args for args, _ in calls if args[:2] == ["meson", "setup"])
            self.assertIn("-Dwin32-threads=enabled", setup)

    def test_ncurses_pkg_config_uses_native_relocatable_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory) / "prefix with spaces"
            pc = prefix / "lib/pkgconfig"
            pc.mkdir(parents=True)
            for name in ("ncursesw", "formw", "menuw", "panelw"):
                (pc / (name + ".pc")).write_text(
                    "prefix=/d/cache/deps\nlibdir=${prefix}/lib\n"
                    f"Name: {name}\nDescription: synthetic ncurses metadata\nVersion: 6.6\n"
                    "Cflags: -I/d/cache/deps/include\nLibs: -L/d/cache/deps/lib -lncursesw\n"
                )
            with patch.object(bootstrap, "unix_path", return_value="/d/cache/deps"):
                bootstrap.relocate_ncurses_pc(prefix)
                # Cached prefixes pass through the same normalization again.
                bootstrap.relocate_ncurses_pc(prefix)
            env = {"PKG_CONFIG_LIBDIR": str(pc)}
            for name in ("ncursesw", "formw"):
                flags = shlex.split(
                    subprocess.check_output(
                        ["pkg-config", "--cflags", "--libs", name], env=env, text=True
                    )
                )
                self.assertIn("-I" + str(prefix / "include"), flags)
                self.assertIn("-L" + str(prefix / "lib"), flags)
                self.assertNotIn("/d/cache/deps", (pc / (name + ".pc")).read_text())

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

    def test_source_archive_marks_container_checkout_safe(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            completed = package.subprocess.CompletedProcess(
                args=["git"], returncode=0, stdout=b"tar"
            )
            with (
                patch.dict(package.os.environ, {"GITHUB_ACTIONS": "true"}),
                patch.object(package.subprocess, "run", return_value=completed) as run,
            ):
                package.source_archive(output, "0.2.0")
            args = run.call_args.args[0]
            self.assertEqual(args[:3], ["git", "-c", f"safe.directory={ROOT}"])
            self.assertIn("dist/flatpak/build-dependencies.json", args)

    def test_semver_and_incomplete_assets_fail_closed(self):
        match = re.search(
            r"project\(\s*papo_tui\s+VERSION\s+([^\s)]+)",
            (ROOT / "CMakeLists.txt").read_text(),
        )
        self.assertIsNotNone(match)
        version = match.group(1)
        self.assertEqual(release.validate_tag("v" + version), version)
        major, minor, patch = (int(part) for part in version.split("."))
        wrong_version = f"v{major + 1}.{minor}.{patch}"
        for tag in ("0.1.2", "v01.1.0", "v0.1", "v0.1.0-beta", wrong_version):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                release.validate_tag(tag)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(ValueError):
                release.check_assets(root, version)
            self.assertFalse((root / "SHA256SUMS").exists())
            for name in release.assets(version):
                (root / name).write_bytes(name.encode())
            release.check_assets(root, version)
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

    def test_package_requires_matching_media_helper(self):
        import struct

        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            modules = stage / "bin/papo-backends"
            modules.mkdir(parents=True)
            (stage / "share/terminfo").mkdir(parents=True)
            (stage / "etc/fonts").mkdir(parents=True)
            (stage / "etc/fonts/fonts.conf").touch()
            header = bytearray(64)
            header[:6] = b"\x7fELF\x02\x01"
            struct.pack_into("<H", header, 18, 62)
            for path in (stage / "bin/papo-tui", modules / "image.so", modules / "video.so"):
                path.write_bytes(header)
            with (
                patch.object(package.subprocess, "check_output", return_value="papo-tui test\n"),
                patch.object(package.subprocess, "run"),
            ):
                with self.assertRaisesRegex(RuntimeError, "missing the media helper"):
                    package.smoke(stage, "test", "x86_64")
                worker = modules / "papo-media-worker"
                struct.pack_into("<H", header, 18, 183)
                worker.write_bytes(header)
                with self.assertRaisesRegex(RuntimeError, "Media helper architecture mismatch"):
                    package.smoke(stage, "test", "x86_64")
                struct.pack_into("<H", header, 18, 62)
                worker.write_bytes(header)
                package.smoke(stage, "test", "x86_64")


if __name__ == "__main__":
    unittest.main()
