"""Animated attachments (GIF, APNG, WebP) play by themselves in the conversation."""

from test_context import protocols

from test_context import main as test_main

from pathlib import Path
import json
import subprocess
import tempfile
import time
import unittest

import terminal_media as media


def animation(name, size, frames):
    loop = ["-plays", "0"] if name.endswith(".apng") else ["-loop", "0"]
    with tempfile.TemporaryDirectory(prefix="papo-anim-") as directory:
        path = Path(directory) / name
        subprocess.run(
            [media.FFMPEG, "-v", "error", "-f", "lavfi", "-i", f"testsrc2=size={size}:rate=15"]
            + ["-frames:v", str(frames), *loop, "-threads", "1", "-y", str(path)],
            check=True,
            capture_output=True,
        )
        return path.read_bytes()


CASES = {
    "large gif": ("big.gif", "480x426", 35, "image/gif"),
    "apng": ("loop.apng", "96x64", 20, "image/png"),
    "animated webp": ("loop.webp", "96x64", 20, "image/webp"),
}


class GifAttachments(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        media.MediaTerminal.setUpClass()

    @classmethod
    def tearDownClass(cls):
        media.MediaTerminal.tearDownClass()

    def setUp(self):
        self.helper = media.MediaTerminal()
        self.addCleanup(self.helper.doCleanups)
        self.addCleanup(self.helper.tearDown)

    def test_animated_attachments_play_without_input(self):
        for label, (name, size, frames, mime) in CASES.items():
            for protocol in protocols("kitty", "sixel", "iterm2", "chafa", "light"):
                with (
                    self.subTest(kind=label, protocol=protocol),
                    self.helper.serve(animation(name, size, frames), mime, name=name) as server,
                ):
                    t = self.helper.open(server, protocol=protocol)
                    try:
                        self.helper.wait_frames(t, 4)
                        self.helper.settled(t, lambda: len(set(t.stream.frames)) >= 3, seconds=10)
                    finally:
                        self.assertTrue(t.close())

    def test_large_gif_plays_in_the_viewer_of_a_large_terminal(self):
        name, size, frames, mime = "viewer.gif", "800x600", 48, "image/gif"
        with self.helper.serve(animation(name, size, frames), mime, name=name) as server:
            t = self.helper.open(server, protocol="kitty", size=(60, 200))
            try:
                self.helper.wait_frames(t, 4)
                self.helper.settled(t, lambda: len(set(t.stream.frames)) >= 3, seconds=10)
                t.send("\x01")
                self.helper.wait(t, "Mídia 1/1")
                t.send("\n")
                self.helper.wait(t, "GIF animado")
                mark = len(t.stream.frames)
                self.helper.settled(
                    t, lambda: len(set(t.stream.frames[mark:])) >= 6, seconds=10
                )
                self.assertNotIn("Carregando mídia", "\n".join(t.screen.display))
            finally:
                self.assertTrue(t.close())

    def test_gif_plays_in_light_mode_and_on_the_linux_console(self):
        for term in ("xterm-256color", "linux"):
            for name, size, frames, terminal in (
                ("small.gif", "96x64", 20, None),
                ("big.gif", "800x600", 48, None),
                ("large-terminal.gif", "800x600", 48, (60, 200)),
            ):
                with (
                    self.subTest(term=term, gif=name, terminal=terminal),
                    tempfile.TemporaryDirectory(prefix="papo-light-") as home,
                    self.helper.serve(animation(name, size, frames), "image/gif", name=name) as server,
                ):
                    config = Path(home) / "papo-tui/config.json"
                    config.parent.mkdir()
                    config.write_text(json.dumps(dict(version=4, profiles=[], app_mode="light")))
                    config.chmod(0o600)
                    t = self.helper.open(
                        server,
                        sixel=False,
                        size=terminal,
                        environment={"XDG_CONFIG_HOME": home, "TERM": term},
                    )
                    try:
                        deadline = time.monotonic() + 10
                        while not any("/attachments/" in r[1] for r in server.requests):
                            self.assertLess(time.monotonic(), deadline, "GIF was not requested")
                            t.read(0.02)
                        seen = set()
                        deadline = time.monotonic() + 10
                        while len(seen) < 4 and time.monotonic() < deadline:
                            t.read(0.05)
                            seen.add("\n".join(t.screen.display))
                        self.assertGreaterEqual(len(seen), 4, "\n".join(t.screen.display))
                    finally:
                        self.assertTrue(t.close())


if __name__ == "__main__":
    test_main(2)
