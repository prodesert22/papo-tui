"""Cards, authenticated media and libmpv through a PTY Sixel responder.

The responder verifies emitted protocol bytes and screen controls, not an
actual emulator's pixel composition. Actual terminal evidence is separate.
"""

import base64
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from wcwidth import wcswidth

FFMPEG = sys.argv.pop(2)
from history_mock import HistoryMock, uuid  # noqa: E402 - path and test arguments are prepared above
from test_terminal import Terminal, TerminalStream  # noqa: E402 - path and test arguments are prepared above
import test_history_terminal as history_tests  # noqa: E402 - path and test arguments are prepared above


class GraphicsStream:
    def __init__(self, screen, *, include_icons=True):
        self.include_icons = include_icons
        self.control_ids = set()
        self.kitty_control = False
        self.stream = TerminalStream(screen)
        self.pending = b""
        self.frames = []
        self.frame_positions = []
        self.protocols = []
        self.kitty_frames = []
        self.live_ids = set()
        self.live_placements = set()
        self.deleted_ids = []
        self.placements = []
        self.placement_positions = []
        self.screen = screen
        self.synchronizations = 0
        self.kitty_header = None
        self.kitty_parts = []

    def feed(self, data):
        self.synchronizations += data.count(b"\x1b[?2026h")
        self.pending += data
        while self.pending:
            markers = (b"\x1bP", b"\x1b_G", b"\x1b]1337;")
            positions = [self.pending.find(marker) for marker in markers]
            start = min((position for position in positions if position >= 0), default=-1)
            if start < 0:
                keep = max(
                    (
                        n
                        for marker in markers
                        for n in range(1, len(marker))
                        if self.pending.endswith(marker[:n])
                    ),
                    default=0,
                )
                size = len(self.pending) - keep
                self.stream.feed(self.pending[:size])
                self.pending = self.pending[size:]
                return
            if start:
                self.stream.feed(self.pending[:start])
                self.pending = self.pending[start:]
            iterm = self.pending.startswith(b"\x1b]1337;")
            terminator = b"\a" if iterm else b"\x1b\\"
            end = self.pending.find(terminator, 2)
            if end < 0:
                return
            sequence = self.pending[: end + len(terminator)]
            self.pending = self.pending[end + len(terminator) :]
            if iterm and not sequence.startswith(b"\x1b]1337;File="):
                # OSC 1337 also carries the Kitty focus marker for shortcuts.
                # Only File reports contain image pixels.
                self.stream.feed(sequence)
                continue
            if sequence.startswith(b"\x1b_G"):
                header, _, data = sequence[3:-2].partition(b";")
                values = dict(field.split(b"=", 1) for field in header.split(b",") if b"=" in field)
                if values.get(b"a") == b"d":
                    image_id = int(values[b"i"])
                    if image_id in self.control_ids and not self.include_icons:
                        continue
                    self.deleted_ids.append(image_id)
                    if b"p" in values:
                        self.live_placements.discard((image_id, int(values[b"p"])))
                    else:
                        self.live_placements = {
                            pair for pair in self.live_placements if pair[0] != image_id
                        }
                    if not any(pair[0] == image_id for pair in self.live_placements):
                        self.live_ids.discard(image_id)
                elif values.get(b"a") == b"p":
                    if int(values[b"i"]) in self.control_ids and not self.include_icons:
                        continue
                    self.placements.append(values)
                    self.placement_positions.append((self.screen.cursor.y, self.screen.cursor.x))
                    self.live_ids.add(int(values[b"i"]))
                    self.live_placements.add((int(values[b"i"]), int(values[b"p"])))
                elif values.get(b"a") in (b"T", b"t"):
                    self.kitty_header = values
                    self.kitty_parts = []
                    # Media regression assertions concern attachment/embed/player
                    # pixels, rather than the newly graphical composer button.
                    self.kitty_control = self.screen.cursor.x == self.screen.columns - 7
                    if self.kitty_control:
                        self.control_ids.add(int(values[b"i"]))
                elif self.kitty_header is not None:
                    if data:
                        self.kitty_parts.append(data)
                    if values.get(b"m") == b"0":
                        pixels = base64.b64decode(b"".join(self.kitty_parts), validate=True)
                        if self.kitty_control and not self.include_icons:
                            self.kitty_header = None
                            continue
                        self.kitty_frames.append((self.kitty_header, pixels))
                        if self.kitty_header[b"a"] == b"T":
                            self.live_ids.add(int(self.kitty_header[b"i"]))
                        self.frames.append(pixels)
                        self.frame_positions.append((self.screen.cursor.y, self.screen.cursor.x))
                        self.protocols.append("kitty")
                        self.kitty_header = None
            else:
                if not self.include_icons and self.screen.cursor.x == self.screen.columns - 7:
                    continue
                self.frames.append(sequence)
                self.frame_positions.append((self.screen.cursor.y, self.screen.cursor.x))
                self.protocols.append("iterm2" if iterm else "sixel")


class MediaTerminal(unittest.TestCase):
    wait = history_tests.HistoryTerminal.wait

    @classmethod
    def setUpClass(cls):
        cls.assets = tempfile.TemporaryDirectory(prefix="papo-media-fixtures-")
        cls.images = {}
        for ext in ("png", "jpg", "webp", "gif"):
            path = Path(cls.assets.name) / ("image." + ext)
            subprocess.run(
                [
                    FFMPEG,
                    "-v",
                    "error",
                    "-f",
                    "lavfi",
                    "-i",
                    "color=c=red:s=16x12",
                    "-frames:v",
                    "1",
                    "-threads",
                    "1",
                    "-y",
                    str(path),
                ],
                check=True,
                capture_output=True,
            )
            cls.images[ext] = path.read_bytes()
        path = Path(cls.assets.name) / "tall.png"
        subprocess.run(
            [
                FFMPEG,
                "-v",
                "error",
                "-f",
                "lavfi",
                "-i",
                "color=c=red:s=16x96",
                "-frames:v",
                "1",
                "-threads",
                "1",
                "-y",
                str(path),
            ],
            check=True,
            capture_output=True,
        )
        cls.tall_image = path.read_bytes()
        cls.large_images = {}
        for shape, size in (("landscape", "1000x500"), ("portrait", "1000x1500")):
            path = Path(cls.assets.name) / (shape + ".png")
            subprocess.run(
                [
                    FFMPEG,
                    "-v",
                    "error",
                    "-f",
                    "lavfi",
                    "-i",
                    "testsrc2=size=" + size,
                    "-frames:v",
                    "1",
                    "-threads",
                    "1",
                    "-y",
                    str(path),
                ],
                check=True,
                capture_output=True,
            )
            cls.large_images[shape] = path.read_bytes()
        path = Path(cls.assets.name) / "animated.gif"
        subprocess.run(
            [
                FFMPEG,
                "-v",
                "error",
                "-f",
                "lavfi",
                "-i",
                "color=red:s=16x12:d=0.2:r=5",
                "-f",
                "lavfi",
                "-i",
                "color=blue:s=16x12:d=0.2:r=5",
                "-filter_complex",
                "[0:v][1:v]concat=n=2:v=1:a=0",
                "-frames:v",
                "2",
                "-loop",
                "0",
                "-threads",
                "1",
                "-y",
                str(path),
            ],
            check=True,
            capture_output=True,
        )
        cls.animated_gif = path.read_bytes()
        path = Path(cls.assets.name) / "video.mp4"
        subprocess.run(
            [
                FFMPEG,
                "-v",
                "error",
                "-f",
                "lavfi",
                "-i",
                "color=c=blue:s=32x24:r=10",
                "-f",
                "lavfi",
                "-i",
                "sine=frequency=440:sample_rate=44100",
                "-t",
                "3",
                "-c:v",
                "mpeg4",
                "-c:a",
                "aac",
                "-threads",
                "1",
                "-y",
                str(path),
            ],
            check=True,
            capture_output=True,
        )
        cls.video = path.read_bytes()

    @classmethod
    def tearDownClass(cls):
        cls.assets.cleanup()

    def open(self, server, sixel=True, environment=None, protocol=None, size=None):
        unset = ()
        if protocol == "iterm2":
            environment = dict(environment or {}, TERM_PROGRAM="iTerm.app", LC_TERMINAL="iTerm2")
            unset = (
                "KITTY_PID",
                "KITTY_WINDOW_ID",
                "KONSOLE_VERSION",
                "VTE_VERSION",
                "GHOSTTY_BIN_DIR",
                "WEZTERM_EXECUTABLE",
                "TMUX",
                "STY",
            )
        t = Terminal(environment=environment, unset_environment=unset)
        if size:
            t.resize(*size)
        if not hasattr(self, "terminals"):
            self.terminals = []
        self.terminals.append(t)
        self.addCleanup(lambda: t.close() if t.process.poll() is None else None)
        stream = GraphicsStream(t.screen, include_icons=getattr(self, "include_icons", False))
        stream.feed(bytes(t.output))
        t.stream = stream
        if protocol == "kitty":
            query = re.search(rb"\x1b_Gi=(\d+),s=1,v=1,a=q", t.output)
            self.assertIsNotNone(query)
            t.send("\x1b_Gi=" + query[1].decode() + ";OK\x1b\\\x1b[?62;22c\x1b[6;16;8t")
        elif protocol == "iterm2":
            t.send("\x1b[?62;22c\x1b[6;16;8t")
        elif sixel:
            t.send("\x1b[?62;4;22c\x1b[6;16;8t")
        t.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(t, "geral")
        t.send("\x1bOB\x1bOB\n")
        self.wait(t, "Conversar em #geral")
        return t

    def tearDown(self):
        for terminal in getattr(self, "terminals", []):
            self.assertEqual(terminal.process.returncode, 0, "Client exited with an error")
            for diagnostic in (b"AddressSanitizer", b"LeakSanitizer", b"runtime error:"):
                self.assertNotIn(
                    diagnostic, terminal.output, "Sanitizer diagnostic in client output"
                )

    def serve(self, content, mime, *, embed=False, slow=False):
        s = HistoryMock(count=1)
        parent = s.route
        s.messages[0]["attachments"] = (
            []
            if embed
            else [
                {
                    "id": uuid(901),
                    "mime_type": mime,
                    "original_file_name": "Fixture",
                    "size_bytes": len(content),
                    "moderation_status": "clean",
                }
            ]
        )

        def route(method, path, body):
            if "/attachments/" in path or "/embeds/" in path:
                if slow:
                    s.stopping.wait(2)
                if embed:
                    return 200, {
                        "id": uuid(902),
                        "image_data": base64.b64encode(content).decode(),
                        "author_image_data": None,
                    }
                return 200, content, None, None, mime
            return parent(method, path, body)

        s.api_handler = route
        return s

    def wait_frames(self, terminal, count=1):
        deadline = time.monotonic() + 10
        while len(terminal.stream.frames) < count and time.monotonic() < deadline:
            terminal.read(0.03)
        self.assertGreaterEqual(
            len(terminal.stream.frames), count, "\n".join(terminal.screen.display)
        )

    def raster_size(self, terminal, index=-1):
        stream = terminal.stream
        if not stream.frames:
            return 0, 0
        if stream.protocols[index] == "kitty":
            header = stream.kitty_frames[index][0]
            return int(header[b"s"]), int(header[b"v"])
        if stream.protocols[index] == "sixel":
            raster = re.search(rb'"\d+;\d+;(\d+);(\d+)', stream.frames[index])
            self.assertIsNotNone(raster)
            return int(raster[1]), int(raster[2])
        payload = base64.b64decode(stream.frames[index].split(b":", 1)[1][:-1], validate=True)
        offset = int.from_bytes(payload[4:8], "little")
        count = int.from_bytes(payload[offset : offset + 2], "little")
        tags = {
            int.from_bytes(payload[pos : pos + 2], "little"): int.from_bytes(
                payload[pos + 8 : pos + 12], "little"
            )
            for pos in range(offset + 2, offset + 2 + count * 12, 12)
        }
        return tags[256], tags[257]

    def wait_raster(self, terminal, predicate):
        deadline = time.monotonic() + 10
        while not predicate(*self.raster_size(terminal)) and time.monotonic() < deadline:
            terminal.read(0.03)
        size = self.raster_size(terminal)
        self.assertTrue(predicate(*size), (size, "\n".join(terminal.screen.display)))
        return size

    def test_large_images_follow_history_and_modal_viewports(self):
        for protocol in ("sixel", "kitty", "iterm2"):
            for shape, content in self.large_images.items():
                embed = shape == "portrait"
                with (
                    self.subTest(protocol=protocol, shape=shape),
                    self.serve(content, "image/png", embed=embed) as server,
                ):
                    if embed:
                        server.messages[0]["embeds"] = [
                            {
                                "id": uuid(902),
                                "source_type": "link",
                                "title": "Link preview",
                                "thumbnail": {"mime_type": "image/png"},
                            }
                        ]
                    t = self.open(server, protocol=protocol)
                    try:
                        self.wait_frames(t)
                        t.resize(70, 220)
                        size = self.wait_raster(t, lambda w, h: w > 400 and h > 360)
                        if shape == "landscape":
                            self.assertTrue(1000 <= size[0] < 1008 and 500 <= size[1] < 516, size)
                        else:
                            self.assertEqual(size, (480, 720))
                            self.assertAlmostEqual(size[1] / size[0], 1.5, delta=0.05)
                        if protocol == "kitty":
                            t.read(0.1)
                            placement = t.stream.placements[-1]
                            self.assertEqual(placement[b"y"], b"0")
                            self.assertLessEqual(int(placement[b"h"]), 720)
                            self.assertAlmostEqual(
                                int(placement[b"h"]) / int(placement[b"w"]),
                                0.5 if shape == "landscape" else 1.5,
                                delta=0.01,
                            )
                        count = len(t.stream.frames)
                        placements = len(t.stream.placements)
                        t.send("\x1b[18~\n")
                        self.wait(t, "[Info]")
                        if protocol == "kitty":
                            deadline = time.monotonic() + 10
                            while (
                                len(t.stream.placements) <= placements
                                and time.monotonic() < deadline
                            ):
                                t.read(0.03)
                            self.assertGreater(len(t.stream.placements), placements)
                        else:
                            self.wait_frames(t, count + 1)
                        modal_size = self.wait_raster(
                            t, lambda w, h: h > 720 if shape == "portrait" else w > 1008
                        )
                        self.assertAlmostEqual(
                            modal_size[1] / modal_size[0],
                            0.5 if shape == "landscape" else 1.5,
                            delta=0.05,
                        )
                        t.send("i")
                        self.wait(t, "Propriedades")
                        self.wait_raster(
                            t, lambda w, h: h > 720 if shape == "portrait" else w > 1008
                        )
                        t.resize(28, 100)
                        self.wait_raster(t, lambda w, h: 0 < h <= 256)
                        t.send("\x1b")
                        self.wait(t, "Conversar em #")
                        endpoint = "/embeds/" if embed else "/attachments/"
                        self.assertEqual(sum(endpoint in r[1] for r in server.requests), 1)
                    finally:
                        self.assertTrue(t.close())

    def test_history_height_cap_with_partial_terminal_cells(self):
        with self.serve(self.large_images["portrait"], "image/png") as server:
            t = self.open(server, protocol="kitty")
            try:
                self.wait_frames(t)
                t.resize(70, 220)
                t.send("\x1b[6;25;11t")
                self.wait_raster(t, lambda w, h: 720 <= h <= 750 and 480 <= w <= 495)
                t.read(0.1)
                placement = t.stream.placements[-1]
                self.assertEqual((int(placement[b"w"]), int(placement[b"h"])), (480, 720))
                self.assertEqual(placement[b"y"], b"0")
                # The extra raster pixels only pad the last terminal cell;
                # the Kitty placement must stop at exactly 720 actual pixels.
                self.assertGreater(self.raster_size(t)[1], 720)
            finally:
                self.assertTrue(t.close())

    def test_gif_plays_inline_at_native_size_without_attachment_metadata(self):
        for protocol in ("sixel", "kitty", "iterm2"):
            with (
                self.subTest(protocol=protocol),
                tempfile.TemporaryDirectory() as directory,
                self.serve(self.animated_gif, "image/gif") as server,
            ):
                server.messages[0]["attachments"][0]["thumbnail_id"] = uuid(999)
                t = self.open(server, protocol=protocol, environment={"TMPDIR": directory})
                try:
                    self.wait_frames(t, 4)
                    t.read(0.1)
                    display = "\n".join(t.screen.display)
                    for metadata in (
                        "Anexo:",
                        "Tipo: image/gif",
                        "Moderação:",
                        "[Imagem / F7]",
                        "Fixture",
                    ):
                        self.assertNotIn(metadata, display)
                    self.assertEqual(sum("/attachments/" in r[1] for r in server.requests), 1)
                    self.assertTrue(
                        any(r[1].endswith("/attachments/" + uuid(901)) for r in server.requests)
                    )
                    if protocol == "kitty":
                        self.assertEqual(
                            len({header[b"i"] for header, _ in t.stream.kitty_frames}), 1
                        )
                        self.assertTrue(
                            all(
                                int(header[b"s"]) == 16 and int(header[b"v"]) == 16
                                for header, _ in t.stream.kitty_frames
                            )
                        )
                        # Require distinct red/blue frames without assuming
                        # full intensity in the graphic encoder's output.
                        self.assertTrue(
                            any(
                                pixels[0] > 128 and pixels[2] < 25
                                for _, pixels in t.stream.kitty_frames
                            )
                        )
                        self.assertTrue(
                            any(
                                pixels[2] > 128 and pixels[0] < 25
                                for _, pixels in t.stream.kitty_frames
                            )
                        )
                        self.assertTrue(
                            all(
                                p[b"c"] == b"0" and p[b"r"] == b"0" and p[b"h"] == b"12"
                                for p in t.stream.placements
                            )
                        )
                    t.click(8, 4)
                    self.wait(t, "Conteúdo do outro canal")
                    t.read(0.15)
                    frames = len(t.stream.frames)
                    t.read(0.4)
                    self.assertEqual(len(t.stream.frames), frames)
                    self.assertFalse(t.stream.live_ids)
                finally:
                    self.assertTrue(t.close())
                self.assertEqual(list(Path(directory).iterdir()), [])

    def test_supported_image_formats_emit_sixel_and_cleanup(self):
        for ext, mime in [
            ("png", "image/png"),
            ("jpg", "image/jpeg"),
            ("webp", "image/webp"),
            ("gif", "image/gif"),
        ]:
            with (
                self.subTest(ext=ext),
                tempfile.TemporaryDirectory() as directory,
                self.serve(self.images[ext], mime) as server,
            ):
                t = self.open(server, environment={"TMPDIR": directory})
                try:
                    self.wait_frames(t)
                    self.assertTrue(
                        any(r[1].endswith("/attachments/" + uuid(901)) for r in server.requests)
                    )
                    self.assertNotIn("?62;4;22c", "\n".join(t.screen.display))
                    self.assertNotIn(b"Synthetic!123", t.output)
                    t.send("Rascunho")
                    cursor = (t.screen.cursor.y, t.screen.cursor.x)
                    t.send("\x1b[<64;40;12M")
                    self.assertEqual((t.screen.cursor.y, t.screen.cursor.x), cursor)
                finally:
                    self.assertTrue(t.close())
                self.assertEqual(list(Path(directory).iterdir()), [])

    def test_no_sixel_keeps_metadata_without_downloading(self):
        with self.serve(self.images["png"], "image/png") as server:
            t = self.open(server, sixel=False)
            try:
                self.wait(t, "Fixture")
                t.read(0.2)
                self.assertFalse(any("/attachments/" in r[1] for r in server.requests))
                t.send("\x1b[18~\n")
                self.wait(t, "Este terminal não oferece")
                t.send("\x1b")
                self.wait(t, "Conversar em #")
            finally:
                self.assertTrue(t.close())

    def test_kitty_image_formats_use_graphics_pixels_and_delete_on_switch(self):
        for ext, mime in [
            ("png", "image/png"),
            ("jpg", "image/jpeg"),
            ("webp", "image/webp"),
            ("gif", "image/gif"),
        ]:
            with self.subTest(ext=ext), self.serve(self.images[ext], mime) as server:
                t = self.open(server, protocol="kitty")
                try:
                    self.wait_frames(t)
                    self.assertEqual(set(t.stream.protocols), {"kitty"})
                    header, pixels = t.stream.kitty_frames[-1]
                    self.assertEqual(header[b"f"], b"32")
                    self.assertEqual(header[b"q"], b"2")
                    self.assertEqual(len(pixels), int(header[b"s"]) * int(header[b"v"]) * 4)
                    self.assertGreater(pixels[0], 230)
                    self.assertLess(pixels[1], 25)
                    self.assertLess(pixels[2], 25)
                    self.assertNotIn("OK", "\n".join(t.screen.display))
                    live = t.stream.live_ids.copy()
                    self.assertTrue(live)
                    t.click(8, 4)
                    self.wait(t, "Conteúdo do outro canal")
                    self.assertFalse(t.stream.live_ids)
                    self.assertTrue(live.issubset(t.stream.deleted_ids))
                finally:
                    self.assertTrue(t.close())

    def test_f7_selects_attachment_with_arrows_and_right_click_occludes_media(self):
        with self.serve(self.images["png"], "image/png") as server:
            item = dict(
                server.messages[0]["attachments"][0],
                id=uuid(903),
                original_file_name="Second image",
            )
            server.messages[0]["attachments"].append(item)
            t = self.open(server, protocol="kitty")
            try:
                self.wait_frames(
                    t
                )  # Identical bytes share one encoded image across two placements.
                t.send("Draft preserved\x1b[18~")
                self.wait(t, "Mídia 1/2")
                self.assertNotIn("Escolha uma mídia", "\n".join(t.screen.display))
                t.send("\x1bOC")
                self.wait(t, "Mídia 2/2")
                t.send("\n")
                self.wait(t, "[Info]")
                self.wait(t, "Second image")
                t.send("\x1b")
                self.wait(t, "Mídia 2/2")
                t.send("\x1b")
                self.wait(t, "Draft preserved")
                y, x = t.stream.placement_positions[-1]
                visible = t.stream.live_placements.copy()
                t.send(f"\x1b[<2;{x + 1};{y + 1}M\x1b[<2;{x + 1};{y + 1}m")
                self.wait(t, "Responder")
                self.assertTrue(
                    visible - t.stream.live_placements, "Menu must cover overlapping images"
                )
                t.send("\x1b")
                self.wait(t, "Draft preserved")
                self.assertEqual(server.message_count, 0)
            finally:
                self.assertTrue(t.close())

    def test_emoji_and_reaction_modals_preserve_uncovered_attachment_pixels(self):
        for protocol in ("kitty", "sixel", "iterm2"):
            for key, title in (("\x1bOS", "Emojis · Esc voltar"), ("\x1b[18~E", "Quem reagiu")):
                with (
                    self.subTest(protocol=protocol, modal=title),
                    self.serve(self.large_images["landscape"], "image/png") as server,
                ):
                    parent = server.api_handler

                    def route(method, path, body):
                        if method == "GET" and path.split("?")[0].endswith("/reactions"):
                            return 200, {
                                "message_id": server.messages[0]["id"],
                                "reactions": [],
                                "has_more": False,
                            }
                        return parent(method, path, body)

                    server.api_handler = route
                    t = self.open(server, protocol=protocol, size=(48, 160))
                    try:
                        self.wait_raster(t, lambda w, h: w > 600 and h > 300)
                        first_frame = len(t.stream.frames)
                        first_placement = len(t.stream.placements)
                        t.send(key)
                        self.wait(t, title)
                        display = t.screen.display
                        top = next(i for i, line in enumerate(display) if title in line)
                        title_x = wcswidth(display[top][: display[top].index(title)])
                        left = next(
                            x
                            for x in range(title_x, -1, -1)
                            if t.screen.buffer[top][x].data in ("l", "┌")
                        )
                        right = (
                            next(
                                x
                                for x in range(title_x, t.screen.columns)
                                if t.screen.buffer[top][x].data in ("k", "┐")
                            )
                            + 1
                        )
                        bottom = (
                            next(
                                y
                                for y in range(top + 1, t.screen.lines)
                                if t.screen.buffer[y][left].data in ("m", "└")
                                and t.screen.buffer[y][right - 1].data in ("j", "┘")
                            )
                            + 1
                        )

                        def background_parts():
                            if protocol == "kitty":
                                dimensions = {
                                    int(h[b"i"]): (int(h[b"s"]), int(h[b"v"]))
                                    for h, _ in t.stream.kitty_frames
                                }
                                images = [
                                    (dimensions.get(int(p[b"i"]), (0, 0)), position)
                                    for p, position in zip(
                                        t.stream.placements[first_placement:],
                                        t.stream.placement_positions[first_placement:],
                                    )
                                    if (int(p[b"i"]), int(p[b"p"])) in t.stream.live_placements
                                ]
                            else:
                                images = [
                                    (self.raster_size(t, i), t.stream.frame_positions[i])
                                    for i in range(first_frame, len(t.stream.frames))
                                ]
                            return [
                                (size, point)
                                for size, point in images
                                if max(size) > 64 and not (size[0] > 600 and size[1] > 300)
                            ]

                        deadline = time.monotonic() + 10
                        while not background_parts() and time.monotonic() < deadline:
                            t.read(0.03)
                        parts = background_parts()
                        self.assertTrue(
                            parts, "Uncovered attachment pixels disappeared behind the modal"
                        )
                        for (w, h), (y, x) in parts:
                            rows, columns = (h + 15) // 16, (w + 7) // 8
                            self.assertTrue(
                                y + rows <= top or y >= bottom or x + columns <= left or x >= right,
                                (
                                    protocol,
                                    title,
                                    (y, x, rows, columns),
                                    (top, left, bottom, right),
                                ),
                            )
                        t.send("\x1b")
                        self.wait_raster(t, lambda w, h: w > 600 and h > 300)
                        self.assertEqual(sum("/attachments/" in r[1] for r in server.requests), 1)
                    finally:
                        self.assertTrue(t.close())

    def test_visible_video_has_automatic_static_poster_and_reuses_download(self):
        for protocol in ("kitty", "sixel", "iterm2"):
            with self.subTest(protocol=protocol), self.serve(self.video, "video/mp4") as server:
                t = self.open(server, protocol=protocol)
                try:
                    self.wait_frames(t)
                    self.wait(t, "▶ Fixture")
                    display = "\n".join(t.screen.display)
                    self.assertNotIn("Anexo:", display)
                    self.assertNotIn("Tipo:", display)
                    self.assertNotIn("Reproduzindo", display)
                    count = len(t.stream.frames)
                    t.read(0.3)
                    self.assertEqual(len(t.stream.frames), count, "Poster must remain static")
                    t.send("\x1b[18~")
                    self.wait(t, "Mídia 1/1")
                    self.assertNotIn("[Info]", "\n".join(t.screen.display))
                    t.send("\n")
                    self.wait(t, "Reproduzindo")
                    self.assertEqual(sum("/attachments/" in r[1] for r in server.requests), 1)
                    t.send("\x1b")
                    self.wait(t, "↑↓ mensagens")
                finally:
                    self.assertTrue(t.close())

    def test_kitty_video_pause_seek_resize_and_cleanup(self):
        with (
            tempfile.TemporaryDirectory() as directory,
            self.serve(self.video, "video/mp4") as server,
        ):
            t = self.open(server, protocol="kitty", environment={"TMPDIR": directory})
            try:
                t.send("Rascunho Kitty\x1b\x1b[18~\n")
                self.wait_frames(t, 2)
                self.wait(t, "Reproduzindo")
                self.assertEqual(set(t.stream.protocols), {"kitty"})
                header, pixels = t.stream.kitty_frames[-1]
                self.assertEqual(len(pixels), int(header[b"s"]) * int(header[b"v"]) * 4)
                center = (int(header[b"v"]) // 2 * int(header[b"s"]) + int(header[b"s"]) // 2) * 4
                self.assertGreater(pixels[center + 2], 230)
                self.assertLess(pixels[center], 25)
                t.send(" ")
                self.wait(t, "Pausado")
                t.send("\x1bOC\x1bOA")
                t.send("m")
                self.assertEqual(len({header[b"i"] for header, _ in t.stream.kitty_frames}), 1)
                self.assertEqual(t.stream.deleted_ids, [])
                self.assertGreater(t.stream.synchronizations, 0)
                frames = len(t.stream.frames)
                t.resize(28, 100)
                self.wait_frames(t, frames + 1)
                self.wait(t, "Pausado")
                self.assertLessEqual(len(t.stream.live_ids), 1)
                player_id = int(t.stream.kitty_frames[-1][0][b"i"])
                t.send("\x1b")
                self.wait(t, "Rascunho Kitty")
                self.assertNotIn(player_id, t.stream.live_ids)
                self.assertLessEqual(len(t.stream.live_ids), 1)  # Static inline poster remains.
                self.assertEqual(sum("/attachments/" in r[1] for r in server.requests), 1)
            finally:
                self.assertTrue(t.close())
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_stationary_images_are_retained_across_focus_and_typing(self):
        for protocol in ("sixel", "kitty", "iterm2"):
            with (
                self.subTest(protocol=protocol),
                self.serve(self.images["png"], "image/png") as server,
            ):
                t = self.open(server, protocol=protocol)
                try:
                    self.wait_frames(t)
                    t.read(0.2)
                    frames = len(t.stream.frames)
                    deletes = len(t.stream.deleted_ids)
                    placements = len(t.stream.placements)
                    for key in ("draft", "\x1b", "\n", "\x1b", "\x1b", "\n"):
                        t.send(key)
                        t.read(0.08)
                        self.assertEqual(
                            len(t.stream.frames), frames, "Stationary picture was retransmitted"
                        )
                        self.assertEqual(
                            len(t.stream.deleted_ids), deletes, "Stationary picture was deleted"
                        )
                        self.assertEqual(
                            len(t.stream.placements), placements, "Stationary picture was replaced"
                        )
                    self.assertEqual(sum("/attachments/" in r[1] for r in server.requests), 1)
                finally:
                    self.assertTrue(t.close())

    def test_identical_embed_update_invalidates_pixels_in_history_and_account(self):
        from realtime_mock import RealtimeMock
        from history_mock import CHANNEL

        with RealtimeMock(count=1) as server:
            embed = {
                "id": uuid(902),
                "source_type": "custom",
                "title": "Mutable preview",
                "thumbnail": {"mime_type": "image/png"},
                "fields": [],
            }
            server.messages[0]["embeds"] = [embed]
            original = server.route
            content = self.images["png"]
            calls = 0

            def route(method, path, body):
                nonlocal calls
                if "/embeds/" in path:
                    calls += 1
                    return 200, {
                        "id": uuid(902),
                        "image_data": base64.b64encode(content).decode(),
                        "author_image_data": None,
                    }
                return original(method, path, body)

            server.api_handler = route
            t = self.open(server, protocol="kitty")
            try:
                self.wait_frames(t)
                t.resize(40, 100)
                t.read(0.15)
                self.assertEqual(calls, 1)
                old_ids = {header[b"i"] for header, _ in t.stream.kitty_frames}
                content = self.images["webp"]
                event = {
                    "type": "message_embeds_update",
                    "channel_id": CHANNEL,
                    "message_id": server.messages[0]["id"],
                    "embeds": [embed],
                }
                server.emit(event)
                deadline = time.monotonic() + 5
                while (
                    calls < 2
                    or not ({header[b"i"] for header, _ in t.stream.kitty_frames} - old_ids)
                ) and time.monotonic() < deadline:
                    t.read(0.03)
                self.assertEqual(calls, 2)
                self.assertTrue({header[b"i"] for header, _ in t.stream.kitty_frames} - old_ids)
                self.assertEqual(sum("/messages" in r[1] for r in server.requests), 1)
                t.send("Preserved draft")
                t.send("\x1b[17~")
                self.wait(t, "[Canais / F6]")
                content = self.images["png"]
                server.emit(event)
                t.read(0.2)
                t.send("\x1b[17~")
                self.wait(t, "geral")
                t.send("\x1bOB\x1bOB\n")
                self.wait(t, "Preserved draft")
                deadline = time.monotonic() + 5
                while calls < 3 and time.monotonic() < deadline:
                    t.read(0.03)
                self.assertEqual(calls, 3)
            finally:
                self.assertTrue(t.close())

    def test_identical_images_have_one_kitty_image_and_four_positions(self):
        for shared_id in (False, True):
            with (
                self.subTest(shared_resource=shared_id),
                self.serve(self.images["png"], "image/png") as server,
            ):
                original = server.messages[0]["attachments"][0]
                server.messages[0]["attachments"] = [
                    dict(original, id=uuid(901 if shared_id else 901 + i)) for i in range(4)
                ]
                t = self.open(server, protocol="kitty")
                try:
                    t.resize(64, 80)
                    deadline = time.monotonic() + 10
                    while len(t.stream.live_placements) < 4 and time.monotonic() < deadline:
                        t.read(0.03)
                    self.assertEqual(len(t.stream.live_placements), 4)
                    self.assertEqual(len(t.stream.live_ids), 1)
                    self.assertEqual(len({p[b"p"] for p in t.stream.placements}), 4)
                    self.assertEqual(
                        sum("/attachments/" in r[1] for r in server.requests), 1 if shared_id else 4
                    )
                    self.assertEqual(len({header[b"i"] for header, _ in t.stream.kitty_frames}), 1)
                    t.send("Shared pixels")
                    self.assertEqual(len(t.stream.live_placements), 4)
                    t.click(8, 4)
                    self.wait(t, "Conteúdo do outro canal")
                    t.read(0.1)
                    self.assertFalse(t.stream.live_ids)
                finally:
                    self.assertTrue(t.close())

    def test_image_gallery_wraps_with_gaps_and_keeps_click_targets(self):
        for embed in (False, True):
            with (
                self.subTest(embed=embed),
                self.serve(self.images["png"], "image/png", embed=embed) as server,
            ):
                if embed:
                    server.messages[0]["embeds"] = [
                        {
                            "id": uuid(901 + i),
                            "source_type": "link",
                            "title": "Preview " + str(i + 1),
                            "thumbnail": {"mime_type": "image/png"},
                        }
                        for i in range(4)
                    ]
                    parent = server.api_handler

                    def route(method, path, body):
                        if "/embeds/" in path:
                            return 200, {
                                "id": path.rsplit("/", 1)[1],
                                "image_data": base64.b64encode(self.images["png"]).decode(),
                                "author_image_data": None,
                            }
                        return parent(method, path, body)

                    server.api_handler = route
                else:
                    original = server.messages[0]["attachments"][0]
                    server.messages[0]["attachments"] = [
                        dict(original, id=uuid(901 + i)) for i in range(4)
                    ]
                t = self.open(server, protocol="kitty")
                try:
                    # One-pixel cells still reserve 48 columns for the avatar.
                    t.resize(50, 180)
                    t.send("\x1b[6;4;1t")

                    def slots():
                        live = {}
                        for position, p in zip(t.stream.placement_positions, t.stream.placements):
                            key = int(p[b"i"]), int(p[b"p"])
                            if key in t.stream.live_placements:
                                live[key] = (*position, int(p[b"w"]), int(p[b"h"]))
                        return sorted(live.values())

                    def wait_rows(rows):
                        deadline = time.monotonic() + 10
                        while time.monotonic() < deadline:
                            found = slots()
                            if (
                                len(found) == 4
                                and len({p[0] for p in found}) == rows
                                and all(p[2:] == (16, 12) for p in found)
                            ):
                                return found
                            t.read(0.03)
                        self.fail("Gallery placements did not settle: " + repr(slots()))

                    wide = wait_rows(1)
                    gap = 6 if embed else 2  # Cards retain two padding columns on either side.
                    self.assertTrue(all(wide[i + 1][1] - wide[i][1] == 16 + gap for i in range(3)))
                    t.resize(50, 124)
                    narrow = wait_rows(2)
                    self.assertEqual(narrow[0][0], narrow[1][0])
                    self.assertEqual(narrow[2][0], narrow[3][0])
                    self.assertEqual(narrow[0][1], narrow[2][1])
                    self.assertEqual(narrow[2][0] - narrow[0][0], 8 if embed else 4)
                    self.assertEqual(len(t.stream.live_ids), 1)
                    endpoint = "/embeds/" if embed else "/attachments/"
                    self.assertEqual(sum(endpoint in r[1] for r in server.requests), 4)
                    t.click(narrow[0][0] + 1, narrow[0][1] + 16)
                    self.assertNotIn("[Info]", "\n".join(t.screen.display))
                    t.click(narrow[1][0] + 1, narrow[1][1] + 8)
                    self.wait(t, "[Info]")
                    t.send("i")
                    self.wait(t, "Propriedades")
                    self.wait(t, uuid(902)[-6:])
                    t.send("\x1b")
                    self.wait(t, "Conversar em #")
                    self.assertEqual(sum(endpoint in r[1] for r in server.requests), 4)
                finally:
                    self.assertTrue(t.close())

    def test_kitty_scrolling_repositions_and_crops_without_retransmission(self):
        with self.serve(self.tall_image, "image/png") as server:
            server.messages[0]["content"] = "\n".join("Line %d" % n for n in range(30))
            t = self.open(server, protocol="kitty")
            try:
                self.wait_frames(t)
                t.read(0.2)
                t.send("\x1b[5~")
                for _ in range(3):
                    t.send("\x1bOB")
                    t.read(0.08)
                self.wait_frames(t, 2)
                t.read(0.15)
                frames = len(t.stream.frames)
                image_id = t.stream.placements[-1][b"i"]
                deletes = len(t.stream.deleted_ids)
                initial = len(t.stream.placements)
                for _ in range(2):
                    t.send("\x1bOB")
                    t.read(0.08)
                self.assertGreater(len(t.stream.placements), initial)
                self.assertEqual(len(t.stream.frames), frames)
                self.assertEqual(len(t.stream.deleted_ids), deletes)
                self.assertEqual({p[b"i"] for p in t.stream.placements[initial:]}, {image_id})
                self.assertEqual(len({p[b"p"] for p in t.stream.placements}), 1)
                self.assertTrue(any(int(p[b"h"]) < 96 for p in t.stream.placements))
                self.assertEqual(sum("/attachments/" in r[1] for r in server.requests), 1)
            finally:
                self.assertTrue(t.close())

    def test_resize_repaints_images_that_already_fit_at_original_size(self):
        for protocol in ("sixel", "kitty", "iterm2"):
            with (
                self.subTest(protocol=protocol),
                self.serve(self.images["png"], "image/png") as server,
            ):
                t = self.open(server, protocol=protocol)
                try:
                    self.wait_frames(t)
                    t.resize(35, 120)
                    self.wait_frames(t, 2)
                    t.read(0.2)
                    frame = t.stream.frames[-1]
                    frames = len(t.stream.frames)
                    t.resize(35, 140)
                    self.wait_frames(t, frames + 1)
                    self.assertEqual(t.stream.frames[-1], frame)
                    self.assertEqual(sum("/attachments/" in r[1] for r in server.requests), 1)
                finally:
                    self.assertTrue(t.close())

    def test_cached_crops_scroll_without_a_loading_placeholder(self):
        for protocol in ("sixel", "iterm2"):
            with (
                self.subTest(protocol=protocol),
                self.serve(self.tall_image, "image/png") as server,
            ):
                server.messages[0]["content"] = "\n".join("Line %d" % n for n in range(30))
                t = self.open(server, protocol=protocol)
                try:
                    self.wait_frames(t)
                    t.send("\x1b[5~")
                    for _ in range(5):
                        t.send("\x1bOB")
                        t.read(0.08)
                        self.assertNotIn("Carregando imagem", "\n".join(t.screen.display))
                    self.assertGreater(len(t.stream.frames), 2)
                    self.assertEqual(sum("/attachments/" in r[1] for r in server.requests), 1)
                finally:
                    self.assertTrue(t.close())

    def test_modal_fits_small_embed_and_properties_reuses_download(self):
        with self.serve(self.images["png"], "image/png", embed=True) as server:
            server.messages[0]["embeds"] = [
                {
                    "id": uuid(902),
                    "source_type": "link",
                    "created_at": "2026-10-14T15:51:18Z",
                    "title": "Link preview",
                    "thumbnail": {"mime_type": "image/png", "size_bytes": len(self.images["png"])},
                    "image": {"mime_type": "image/png"},
                }
            ]
            t = self.open(server, protocol="kitty")
            try:
                self.wait_frames(t)
                t.send("\x1b[18~\n")
                self.wait(t, "[Info]")
                self.wait(t, "Imagem")
                self.wait_raster(t, lambda w, h: w >= 232 and h >= 176)
                t.read(0.1)
                # 80x24: viewer keeps channels at x<26, with outer margins
                # at (30,3). The image area spans x=33..72 and y=6..16.
                self.assertEqual(t.stream.placement_positions[-1], (6, 38))
                placement = t.stream.placements[-1]
                self.assertEqual((int(placement[b"w"]), int(placement[b"h"])), (234, 176))
                self.assertIn("Canais", "\n".join(t.screen.display))
                frames = len(t.stream.frames)
                image_id = t.stream.placements[-1][b"i"]
                t.click(4, 33)
                self.wait(t, "Propriedades")
                t.read(0.15)
                self.wait_frames(t, frames + 1)
                t.read(0.1)
                self.assertEqual(t.stream.placement_positions[-1], (7, 33))
                placement = t.stream.placements[-1]
                self.assertEqual((int(placement[b"w"]), int(placement[b"h"])), (168, 126))
                self.assertIn("image/png", "\n".join(t.screen.display))
                self.assertIn(str(len(self.images["png"])) + " bytes", "\n".join(t.screen.display))
                frames = len(t.stream.frames)
                t.click(4, 33)
                t.read(0.1)
                self.wait_frames(t, frames + 1)
                t.read(0.05)
                self.assertNotIn("Propriedades", "\n".join(t.screen.display))
                self.assertEqual(t.stream.placement_positions[-1], (6, 38))
                self.assertEqual(t.stream.placements[-1][b"i"], image_id)
                # Kitty deleted the inactive placement, so it needs another
                # upload of the same cached variant when its pane returns.
                self.assertEqual(len(t.stream.frames), frames + 1)
                t.click(8, 4)
                t.read(0.2)
                self.assertNotIn("Esc: voltar", "\n".join(t.screen.display))
                self.assertIn("# geral", "\n".join(t.screen.display))
                self.assertEqual(sum("/embeds/" in r[1] for r in server.requests), 1)
                self.assertFalse(
                    any("/channels/" + uuid(11) + "/messages" in r[1] for r in server.requests)
                )
            finally:
                self.assertTrue(t.close())

    def test_kitty_rejected_probe_keeps_text_without_downloads(self):
        with self.serve(self.images["png"], "image/png") as server:
            t = self.open(server, sixel=False)
            try:
                query = re.search(rb"\x1b_Gi=(\d+),s=1,v=1,a=q", t.output)
                self.assertIsNotNone(query)
                t.send("\x1b_Gi=" + query[1].decode() + ";ENOTSUP\x1b\\\x1b[6;16;8t")
                self.wait(t, "Fixture")
                t.read(0.2)
                self.assertFalse(any("/attachments/" in r[1] for r in server.requests))
                self.assertNotIn("ENOTSUP", "\n".join(t.screen.display))
                self.assertFalse(t.stream.frames)
            finally:
                self.assertTrue(t.close())

    def test_kitty_delayed_fragmented_reply_enables_existing_viewport(self):
        with self.serve(self.images["png"], "image/png") as server:
            t = self.open(server, sixel=False)
            try:
                self.wait(t, "Conversar em #")
                query = re.search(rb"\x1b_Gi=(\d+),s=1,v=1,a=q", t.output)
                self.assertIsNotNone(query)
                t.send("\x1b_Gi=" + query[1].decode())
                t.send(";OK\x1b\\")
                t.send("\x1b[6;16;8t")
                self.wait_frames(t)
                self.assertEqual(set(t.stream.protocols), {"kitty"})
                self.assertNotIn("OK", "\n".join(t.screen.display))
                self.assertEqual(sum("/attachments/" in r[1] for r in server.requests), 1)
            finally:
                self.assertTrue(t.close())

    def test_iterm2_inline_pixels_and_video_controls(self):
        with self.serve(self.images["png"], "image/png") as server:
            t = self.open(server, protocol="iterm2")
            try:
                self.wait_frames(t)
                self.assertEqual(set(t.stream.protocols), {"iterm2"})
                payload = base64.b64decode(
                    t.stream.frames[-1].split(b":", 1)[1][:-1], validate=True
                )
                self.assertEqual(payload[:4], b"II*\0")
                first_ifd = int.from_bytes(payload[4:8], "little")
                count = int.from_bytes(payload[first_ifd : first_ifd + 2], "little")
                tags = {}
                for offset in range(first_ifd + 2, first_ifd + 2 + count * 12, 12):
                    tags[int.from_bytes(payload[offset : offset + 2], "little")] = int.from_bytes(
                        payload[offset + 8 : offset + 12], "little"
                    )
                self.assertLessEqual(tags[256], 640)
                self.assertLessEqual(tags[257], 360)
                self.assertGreater(payload[tags[273]], 230)
                t.send("\x1b[18~\n")
                self.wait_frames(t, 2)
                t.send("\x1b")
                self.wait(t, "Conversar em #")
            finally:
                self.assertTrue(t.close())
        with self.serve(self.video, "video/mp4") as server:
            t = self.open(server, protocol="iterm2")
            try:
                t.send("\x1b[18~\n")
                self.wait_frames(t, 2)
                self.wait(t, "Reproduzindo")
                t.send(" ")
                self.wait(t, "Pausado")
                t.resize(28, 100)
                self.wait(t, "Pausado")
                self.assertEqual(set(t.stream.protocols), {"iterm2"})
                t.send("\x1b")
                self.wait(t, "Conversar em #")
            finally:
                self.assertTrue(t.close())

    def test_channel_navigation_preserves_image_and_reopening_uses_cache(self):
        with self.serve(self.images["png"], "image/png") as server:
            t = self.open(server)
            try:
                self.wait_frames(t)
                t.send("Rascunho com imagem\x1b")
                t.click(5, 4)
                t.read(0.2)
                conversation = [row[26:] for row in t.screen.display[5:-3]]
                frames = len(t.stream.frames)
                requests = [
                    r for r in server.requests if "/messages" in r[1] or "/attachments/" in r[1]
                ]
                for key in ("\x1bOB", "\x1bOA", "\x1bOH", "\x1bOF", "\x1b[5~", "\x1b[6~"):
                    t.send(key)
                    self.assertEqual([row[26:] for row in t.screen.display[5:-3]], conversation)
                    self.assertEqual(len(t.stream.frames), frames, "Navigation redrew the image")
                t.send("\x1bOH\x1bOB\n")
                self.wait(t, "Rascunho com imagem")
                t.send("\x1b[18~\n")
                self.wait(t, "Imagem")
                self.wait_frames(t, frames + 1)
                self.assertEqual(
                    [r for r in server.requests if "/messages" in r[1] or "/attachments/" in r[1]],
                    requests,
                )
                t.send("\x1b")
                self.wait(t, "Rascunho com imagem")
            finally:
                self.assertTrue(t.close())

    def test_embed_author_and_thumbnail_share_header(self):
        with self.serve(self.images["png"], "image/png", embed=True) as server:
            parent = server.api_handler

            def route(method, path, body):
                if "/embeds/" in path:
                    encoded = base64.b64encode(self.images["png"]).decode()
                    return 200, {
                        "id": uuid(902),
                        "image_data": encoded,
                        "author_image_data": encoded,
                    }
                return parent(method, path, body)

            server.api_handler = route
            server.messages[0]["content"] = None
            server.messages[0]["embeds"] = [
                {
                    "id": uuid(902),
                    "source_type": "custom",
                    "author": {"name": "Info", "media": {"mime_type": "image/png"}},
                    "site_name": "Info",
                    "title": "Example Title",
                    "url": "https://example.com",
                    "thumbnail": {"mime_type": "image/png"},
                    "footer": {"text": "Example Footer"},
                }
            ]
            t = self.open(server, protocol="kitty")
            try:
                t.resize(35, 120)
                t.send("\x1bOH")
                self.wait(t, "Info")
                self.wait_frames(t, 2)
                t.read(0.15)
                sizes = {
                    (int(header[b"c"]), int(header[b"r"])) for header, _ in t.stream.kitty_frames
                }
                self.assertIn((4, 2), sizes)
                self.assertIn((12, 4), sizes)
                self.assertEqual(sum("/embeds/" in r[1] for r in server.requests), 1)
                display = "\n".join(t.screen.display)
                self.assertEqual(display.count("Info"), 1)
                self.assertNotIn("Avatar do autor", display)
                self.assertNotIn("[Imagem / F7]", display)
                self.assertNotIn("https://example.com", display)
                self.assertNotIn("Sem conteúdo textual", display)
                self.assertNotIn("Rodapé:", display)
            finally:
                self.assertTrue(t.close())

    def test_embed_thumbnail_base64_and_unavailable_extra_image(self):
        with self.serve(self.images["png"], "image/png", embed=True) as server:
            server.messages[0]["embeds"] = [
                {
                    "id": uuid(902),
                    "title": "Embed fixture",
                    "thumbnail": {"mime_type": "image/png"},
                    "image": {"mime_type": "image/png"},
                    "footer": {"text": "Footer"},
                }
            ]
            t = self.open(server)
            try:
                # Keep the thumbnail and fallback notice visible together;
                # message indentation reduces the card's available width.
                t.resize(30, 100)
                self.wait_frames(t)
                self.wait(t, "indisponível no backend")
                self.assertEqual(sum("/embeds/" in r[1] for r in server.requests), 1)
            finally:
                self.assertTrue(t.close())

    def test_corrupt_image_and_cancelled_video_leave_no_files(self):
        with (
            tempfile.TemporaryDirectory() as directory,
            self.serve(b"not an image", "image/png") as server,
        ):
            t = self.open(server, environment={"TMPDIR": directory})
            try:
                self.wait(t, "Imagem inválida")
            finally:
                self.assertTrue(t.close())
            self.assertEqual(list(Path(directory).iterdir()), [])
        with (
            tempfile.TemporaryDirectory() as directory,
            self.serve(self.video, "video/mp4", slow=True) as server,
        ):
            t = self.open(server, environment={"TMPDIR": directory})
            try:
                self.wait(t, "▶ Fixture")
                t.send("\x1b[18~\n")
                self.wait(t, "Carregando mídia")
                t.send("\x1b")
                self.wait(t, "Conversar em #geral")
                t.send("Preservado")
                self.wait(t, "Preservado")
            finally:
                self.assertTrue(t.close())
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_channel_switch_cancels_late_media(self):
        with (
            tempfile.TemporaryDirectory() as directory,
            self.serve(self.images["png"], "image/png", slow=True) as server,
        ):
            t = self.open(server, environment={"TMPDIR": directory})
            try:
                self.wait(t, "Carregando imagem")
                t.click(8, 4)
                self.wait(t, "Conteúdo do outro canal")
                t.read(2.2)
                self.assertEqual(t.stream.frames, [])
                self.assertNotIn("Fixture", "\n".join(t.screen.display))
            finally:
                self.assertTrue(t.close())
            self.assertEqual(list(Path(directory).iterdir()), [])

    def assert_blue_video_size(self, terminal, expected):
        header, pixels = terminal.stream.kitty_frames[-1]
        width, height = int(header[b"s"]), int(header[b"v"])
        blue = [i for i in range(width * height) if pixels[4 * i + 2] > 200 and pixels[4 * i] < 30]
        self.assertTrue(blue, "Video frame contains no blue pixels")
        columns = [i % width for i in blue]
        rows = [i // width for i in blue]
        content = (max(columns) - min(columns) + 1, max(rows) - min(rows) + 1)
        placement = next(p for p in reversed(terminal.stream.placements) if p[b"i"] == header[b"i"])
        if int(placement.get(b"c", 0)) and int(placement.get(b"r", 0)):
            content = (
                content[0] * int(placement[b"c"]) * 8 / int(placement[b"w"]),
                content[1] * int(placement[b"r"]) * 16 / int(placement[b"h"]),
            )
        for actual, wanted, cell in zip(content, expected, (8, 16)):
            self.assertAlmostEqual(actual, wanted, delta=cell)

    def wait_video_size(self, terminal, expected):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            for placement in reversed(terminal.stream.placements[-5:]):
                if (int(placement.get(b"c", 0)) * 8, int(placement.get(b"r", 0)) * 16) == expected:
                    return
            terminal.read(0.02)
        self.fail(f"Video display did not reach {expected}: {terminal.stream.placements[-1:]}")

    def test_small_video_enlarges_to_fit_modal(self):
        for protocol in ("sixel", "kitty", "iterm2"):
            with (
                self.subTest(protocol=protocol),
                self.serve(self.video, "video/mp4") as server,
            ):
                t = self.open(server, protocol=protocol)
                try:
                    t.send("Rascunho\x1b\x1b[18~\n")
                    self.wait_frames(t, 2)
                    # Fit the 4:3 video; Kitty/iTerm2 pad the final terminal cell.
                    expected = (234 if protocol == "sixel" else 240, 176)
                    if protocol == "kitty":
                        self.wait_video_size(t, expected)
                        self.assertEqual(self.raster_size(t), (32, 32))
                        self.assert_blue_video_size(t, (234, 176))
                    else:
                        self.assertEqual(self.raster_size(t), expected)
                    self.wait(t, "Reproduzindo")
                    t.send(" ")
                    self.wait(t, "Pausado")
                    t.resize(70, 220)
                    expected = (1130 if protocol == "sixel" else 1136, 848)
                    if protocol == "kitty":
                        self.wait_video_size(t, expected)
                        self.assert_blue_video_size(t, (1130, 848))
                    else:
                        self.wait_raster(t, lambda w, h: (w, h) == expected)
                    t.send("i")
                    self.wait(t, "Propriedades")
                    self.wait(t, "32 × 24 px")
                    expected = (1128, 846 if protocol == "sixel" else 848)
                    if protocol == "kitty":
                        self.wait_video_size(t, expected)
                    else:
                        self.wait_raster(t, lambda w, h: (w, h) == expected)
                    t.send("\x1b")
                    self.wait(t, "Rascunho")
                    self.assertEqual(sum("/attachments/" in r[1] for r in server.requests), 1)
                finally:
                    self.assertTrue(t.close())

    def test_video_fits_modal_proportionally_and_resizes_while_paused(self):
        for name, filters, original in (
            ("landscape", "scale=1280:720,setsar=1", (1280, 720)),
            ("portrait", "scale=720:1280,setsar=1", (720, 1280)),
            ("anamorphic", "setsar=2", (64, 24)),
        ):
            with self.subTest(shape=name):
                path = Path(self.assets.name) / (name + ".mp4")
                subprocess.run(
                    [
                        FFMPEG,
                        "-v",
                        "error",
                        "-i",
                        str(Path(self.assets.name) / "video.mp4"),
                        "-an",
                        "-vf",
                        filters,
                        "-c:v",
                        "mpeg4",
                        "-threads",
                        "1",
                        "-y",
                        str(path),
                    ],
                    check=True,
                    capture_output=True,
                )
                with self.serve(path.read_bytes(), "video/mp4") as server:
                    t = self.open(server, protocol="kitty")
                    try:
                        t.send("\x1b[18~\n")
                        self.wait_frames(t, 2)
                        scale = min(320 / original[0], 176 / original[1])
                        expected = tuple(max(1, int(n * scale)) for n in original)
                        self.wait_video_size(
                            t, ((expected[0] + 7) // 8 * 8, (expected[1] + 15) // 16 * 16)
                        )
                        self.assert_blue_video_size(t, expected)
                        t.send(" ")
                        self.wait(t, "Pausado")
                        t.resize(70, 220)
                        scale = min(1376 / original[0], 848 / original[1])
                        expected = tuple(max(1, int(n * scale)) for n in original)
                        raster = ((expected[0] + 7) // 8 * 8, (expected[1] + 15) // 16 * 16)
                        self.wait_video_size(t, raster)
                        self.assert_blue_video_size(t, expected)
                        self.wait(t, "Pausado")
                        self.assertEqual(sum("/attachments/" in r[1] for r in server.requests), 1)
                    finally:
                        self.assertTrue(t.close())

    def player_controls(self, terminal):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            for row, line in enumerate(terminal.screen.display):
                if "[⏸]" in line or "[▶]" in line:
                    controls = list(re.finditer(r"\[([^]]*)\]", line))
                    if len(controls) >= 3:
                        return row, controls[:3]
            terminal.read(0.03)
        self.fail(
            "Play/pause, timeline and volume controls are missing\n"
            + "\n".join(terminal.screen.display)
        )

    def test_player_bars_mouse_seek_volume_gradient_and_resize(self):
        with self.serve(self.video, "video/mp4") as server:
            t = self.open(server, protocol="kitty", environment={"KITTY_WINDOW_ID": "papo-test"})
            try:
                t.send("Rascunho controles\x1b\x1b[18~\n")
                self.wait_frames(t, 2)
                row, controls = self.player_controls(t)
                t.click(row, controls[0].start() + 1)
                self.wait(t, "Pausado")
                row, controls = self.player_controls(t)
                t.click(row, controls[1].end() - 1)
                self.wait(t, "3 / 3 s")
                t.click(row, controls[1].start())
                self.wait(t, "0 / 3 s")
                t.click(row, controls[0].start() + 1)
                self.wait(t, "Reproduzindo")
                row, controls = self.player_controls(t)
                t.click(row, controls[0].start() + 1)
                self.wait(t, "Pausado")

                t.resize(34, 140)
                row, controls = self.player_controls(t)
                volume = controls[2]
                t.click(row, volume.end() - 1)
                self.wait(t, "100%")
                row, controls = self.player_controls(t)
                volume = controls[2]
                cells = [
                    t.screen.buffer[row][x] for x in range(volume.start() + 1, volume.end() - 1)
                ]
                self.assertTrue(all(cell.data == "█" for cell in cells))
                colors = []
                for fraction in (0.25, 0.75, 0.95):
                    color = cells[int((len(cells) - 1) * fraction)].fg
                    colors.append(tuple(int(color[i : i + 2], 16) for i in (0, 2, 4)))
                green, yellow, red = colors
                self.assertGreater(green[1], green[0])
                self.assertGreater(green[1], green[2])
                self.assertGreaterEqual(yellow[0], 200)
                self.assertGreaterEqual(yellow[1], 180)
                self.assertLess(yellow[2], 100)
                self.assertGreater(red[0], 200)
                self.assertLess(red[1], 150)
                t.send("\x1bOA")
                self.wait(t, "100%")
                t.click(row, volume.start())
                self.wait(t, "  0%")
                t.send("\x1bOB")
                self.wait(t, "  0%")
                row, controls = self.player_controls(t)
                self.assertNotIn("█", controls[2].group(1))
                volume = controls[2]
                t.click(row, volume.start() + 1 + (len(volume.group(1)) - 1) * 3 // 4)
                self.wait(t, " 75%")
                t.send("\x1bOA" * 2)
                self.wait(t, " 85%")
                row, controls = self.player_controls(t)
                for x in range(controls[2].start() + 1, controls[2].end() - 1):
                    cell = t.screen.buffer[row][x]
                    if cell.data == "█":
                        red = int(cell.fg[:2], 16)
                        green = int(cell.fg[2:4], 16)
                        self.assertFalse(red > 200 and green < 150)
                t.send("\x1bOB" * 5)
                self.wait(t, " 60%")
                row, controls = self.player_controls(t)
                for x in range(controls[2].start() + 1, controls[2].end() - 1):
                    cell = t.screen.buffer[row][x]
                    if cell.data == "█":
                        self.assertGreater(int(cell.fg[2:4], 16), int(cell.fg[:2], 16))
                t.send("\x1bOA" * 3)
                self.wait(t, " 75%")
                t.send("m")
                self.wait(t, "Mudo")
                self.assertIn(" 75%", "\n".join(t.screen.display))
                t.send("m")

                t.send("i")
                self.wait(t, "Propriedades")
                row, controls = self.player_controls(t)
                t.click(row, controls[2].end() - 1)
                self.wait(t, "100%")
                t.resize(22, 44)
                status_row = next(i for i, line in enumerate(t.screen.display) if "Pausado" in line)
                row = status_row - 1
                tracks = list(re.finditer(r"\[([^]]*)\]", t.screen.display[row]))
                self.assertGreaterEqual(len(tracks), 2)
                t.click(row, tracks[1].start())
                t.resize(34, 140)
                self.wait(t, "  0%")
                t.send("\x1b")
                self.wait(t, "Rascunho controles")
                self.assertEqual(sum("/attachments/" in r[1] for r in server.requests), 1)
            finally:
                self.assertTrue(t.close())

    def test_kitty_large_video_mouse_release_keyboard_and_frame_cadence(self):
        path = Path(self.assets.name) / "fast-video.mp4"
        subprocess.run(
            [
                FFMPEG,
                "-v",
                "error",
                "-f",
                "lavfi",
                "-i",
                "testsrc2=s=160x90:r=30",
                "-t",
                "8",
                "-c:v",
                "mpeg4",
                "-threads",
                "1",
                "-y",
                str(path),
            ],
            check=True,
            capture_output=True,
        )
        with self.serve(path.read_bytes(), "video/mp4") as server:
            t = self.open(
                server, protocol="kitty", environment={"TERM": "xterm-kitty"}, size=(90, 340)
            )
            try:
                self.assertIn(b"\x1b[?1006", bytes(t.output))
                self.wait(t, "▶ Fixture")
                t.send("\x1b[18~\n")
                self.wait_frames(t, 2)
                row, controls = self.player_controls(t)
                # Release is a distinct packet, as with a real held click. Input
                # must remain live even before it arrives, including beyond X10.
                volume = controls[2]
                self.assertGreater(volume.start(), 223)
                t.send(f"\x1b[<0;{volume.end()};{row + 1}M")
                self.wait(t, "100%")
                t.send("\x1bOB")
                self.wait(t, " 95%")
                t.send(f"\x1b[<0;{volume.end()};{row + 1}m")
                t.send("\x1b[B")
                self.wait(t, " 90%")
                t.click(row, controls[0].start() + 1)
                self.wait(t, "Pausado")
                row, controls = self.player_controls(t)
                self.assertEqual(controls[0].group(), "[▶]")
                t.click(row, controls[1].start())
                self.wait(t, "0 / 8 s")
                t.click(row, controls[0].start() + 1)
                self.wait(t, "Reproduzindo")
                row, controls = self.player_controls(t)
                self.assertEqual(controls[0].group(), "[⏸]")
                start = len(t.stream.frames)
                deadline = time.monotonic() + 3
                while time.monotonic() < deadline:
                    t.read(0.01)
                frames = len(t.stream.frames) - start
                self.assertGreaterEqual(frames, 60, f"Only {frames} frames in three seconds")
                for header, _ in t.stream.kitty_frames:
                    self.assertLessEqual(int(header[b"s"]), 160)
                    self.assertLessEqual(int(header[b"v"]), 96)
                t.click(row, controls[2].start())
                self.wait(t, "  0%")
                t.send("\x1bOA")
                self.wait(t, "  5%")
            finally:
                self.assertTrue(t.close())

    def test_video_modal_frames_pause_seek_resize_and_return(self):
        with (
            tempfile.TemporaryDirectory() as directory,
            self.serve(self.video, "video/mp4") as server,
        ):
            t = self.open(server, environment={"TMPDIR": directory})
            try:
                self.wait(t, "▶ Fixture")
                t.send("Rascunho")
                t.send("\x1b")
                t.send("\x1b[18~\n")
                self.wait_frames(t, 2)
                self.wait(t, "Reproduzindo")
                t.send(" ")
                self.wait(t, "Pausado")
                t.send("\x1bOC\x1bOA")
                t.send("m")
                before_resize = len(t.stream.frames)
                t.resize(28, 100)
                self.wait_frames(t, before_resize + 1)
                self.wait(t, "Pausado")
                t.send("\x1b")
                self.wait(t, "Rascunho")
                self.assertFalse(
                    any(r[0] != "GET" for r in server.requests if "/attachments/" in r[1])
                )
            finally:
                self.assertTrue(t.close())
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_video_keyboard_seek_steps_and_limits_while_paused(self):
        path = Path(self.assets.name) / "seek-video.mp4"
        subprocess.run(
            [
                FFMPEG,
                "-v",
                "error",
                "-f",
                "lavfi",
                "-i",
                "testsrc2=s=32x24:r=10",
                "-t",
                "20",
                "-c:v",
                "mpeg4",
                "-g",
                "100",
                "-threads",
                "1",
                "-y",
                str(path),
            ],
            check=True,
            capture_output=True,
        )
        with self.serve(path.read_bytes(), "video/mp4") as server:
            t = self.open(server, protocol="kitty")
            try:
                t.resize(34, 140)
                t.send("\x1b[18~\n")
                self.wait_frames(t, 2)
                t.send(" ")
                self.wait(t, "Pausado")
                row, controls = self.player_controls(t)
                t.click(row, controls[1].start())
                self.wait(t, "0 / 20 s")
                # One-second steps must work between keyframes too. Both
                # cursor-key encodings retain their five-second steps.
                for keys, position in (
                    (",", 0),
                    (".", 1),
                    ("...", 4),
                    (",", 3),
                    (",,,", 0),
                    ("<", 0),
                    (">", 1),
                    (">>>", 4),
                    ("<", 3),
                    ("<<<", 0),
                    ("\x1bOD", 0),
                    ("\x1bOC", 5),
                    ("\x1b[C\x1b[C", 15),
                    ("\x1b[D", 10),
                    ("\x1bOD\x1bOD\x1bOD", 0),
                    ("\x1bOC" * 5, 20),
                    (".", 20),
                    (",", 19),
                    (".", 20),
                    (">", 20),
                    ("<", 19),
                    (">", 20),
                    ("\x1b[D", 15),
                ):
                    with self.subTest(position=position, keys=repr(keys)):
                        t.send(keys)
                        self.wait(t, f"Pausado · {position} / 20 s")
                        _, controls = self.player_controls(t)
                        self.assertEqual(controls[0].group(), "[▶]")
                t.click(row, controls[1].start())
                self.wait(t, "Pausado · 0 / 20 s")
                t.send(" ")
                self.wait(t, "Reproduzindo")
                t.send("...")
                self.wait(t, "Reproduzindo · 3 / 20 s")
                t.send(",,")
                self.wait(t, "Reproduzindo · 1 / 20 s")
                self.assertEqual(sum("/attachments/" in r[1] for r in server.requests), 1)
            finally:
                self.assertTrue(t.close())

    def test_invalid_video_retry_restores_controls_and_preserves_draft(self):
        with (
            tempfile.TemporaryDirectory() as directory,
            self.serve(b"not a video", "video/mp4") as server,
        ):
            t = self.open(server, protocol="kitty", environment={"TMPDIR": directory})
            try:
                t.resize(34, 140)
                t.send("Rascunho retry\x1b\x1b[18~\n")
                self.wait(t, "Vídeo inválido ou codec indisponível")
                self.assertEqual(sum("/attachments/" in r[1] for r in server.requests), 1)
                parent = server.api_handler

                def route(method, path, body):
                    if "/attachments/" in path:
                        return 200, self.video, None, None, "video/mp4"
                    return parent(method, path, body)

                server.api_handler = route
                t.send("\n")
                self.wait_frames(t, 2)
                self.wait(t, "Reproduzindo")
                row, controls = self.player_controls(t)
                t.click(row, controls[0].start() + 1)
                self.wait(t, "Pausado")
                t.click(row, controls[2].end() - 1)
                self.wait(t, "100%")
                t.send("m")
                self.wait(t, "Mudo")
                t.send("\x1b")
                self.wait(t, "Rascunho retry")
                self.assertEqual(sum("/attachments/" in r[1] for r in server.requests), 2)
            finally:
                self.assertTrue(t.close())
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_video_end_keeps_last_frame_and_can_seek_without_new_http(self):
        with self.serve(self.video, "video/mp4") as server:
            t = self.open(server)
            try:
                t.send("\x1b[18~\n")
                self.wait_frames(t, 2)
                self.wait(t, "Pausado")
                self.assertIn("3 / 3 s", "\n".join(t.screen.display))
                requests = sum("/attachments/" in r[1] for r in server.requests)
                t.send("\x1bOD")
                t.send(" ")
                self.wait(t, "Reproduzindo")
                self.assertEqual(sum("/attachments/" in r[1] for r in server.requests), requests)
            finally:
                self.assertTrue(t.close())

    def test_cards_inline_fields_and_resize(self):
        with HistoryMock(count=1) as server:
            server.messages[0]["content"] = None
            server.messages[0]["embeds"] = [
                {
                    "title": "Card",
                    "color": "#00b0f4",
                    "author": {"name": "Author"},
                    "fields": [
                        {"name": str(i), "value": "Value" + str(i), "inline": True}
                        for i in range(3)
                    ],
                    "footer": {"text": "Footer"},
                }
            ]
            t = self.open(server, sixel=False)
            try:
                t.resize(35, 120)
                t.send("\x1bOH")
                self.wait(t, "Value0")
                self.assertTrue(
                    any(
                        "Value0" in line and "Value1" in line and "Value2" in line
                        for line in t.screen.display
                    )
                )
                display = "\n".join(t.screen.display)
                self.assertNotIn("Sem conteúdo textual", display)
                self.assertNotIn("Rodapé:", display)
                bar_y, bar_x = next(
                    (y, line.index("▎"))
                    for y, line in enumerate(t.screen.display)
                    if "Card" in line
                )
                bar = t.screen.buffer[bar_y][bar_x]
                body = t.screen.buffer[bar_y][bar_x + 2]
                outside = t.screen.buffer[bar_y][bar_x - 1]
                self.assertEqual(bar.bg, body.bg)
                self.assertNotEqual(body.bg, outside.bg)
                self.assertNotEqual(bar.fg, body.fg)
                self.assertTrue(
                    all(
                        t.screen.buffer[y][bar_x].data == "▎"
                        for y, line in enumerate(t.screen.display)
                        if "Author" in line or "Footer" in line
                    )
                )
                for word in ("Author", "Footer"):
                    y, x = next(
                        (y, line.index(word))
                        for y, line in enumerate(t.screen.display)
                        if word in line
                    )
                    self.assertFalse(t.screen.buffer[y][x].bold)
                    self.assertNotEqual(t.screen.buffer[y][x].fg, body.fg)
                t.resize(35, 44)
                self.wait(t, "Value0")
                self.assertFalse(
                    any("Value0" in line and "Value1" in line for line in t.screen.display)
                )
                self.wait(t, "Footer")
                self.assertNotIn("Rodapé:", "\n".join(t.screen.display))
            finally:
                self.assertTrue(t.close())


if __name__ == "__main__":
    unittest.main()
