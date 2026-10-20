"""Cards, authenticated media and libmpv through a PTY Sixel responder.

The responder verifies emitted protocol bytes and screen controls, not an
actual emulator's pixel composition. Actual terminal evidence is separate.
"""

from test_context import argument

import os
import base64
import zlib
import re
import time
import unittest

FFMPEG = os.environ.get("PAPO_TEST_FFMPEG") or argument(1)
from history_mock import HistoryMock, uuid  # noqa: E402 - path and test arguments are prepared above
from terminal_support import Terminal, TerminalStream  # noqa: E402 - path and test arguments are prepared above
import terminal_history as history_tests  # noqa: E402 - path and test arguments are prepared above


def raster_dimensions(protocol, frame):
    if protocol == "kitty":
        return int(frame[b"s"]), int(frame[b"v"])
    if protocol == "sixel":
        raster = re.search(rb'"\d+;\d+;(\d+);(\d+)', frame)
        if raster is None:
            raise AssertionError("Missing Sixel raster dimensions")
        return int(raster[1]), int(raster[2])
    payload = base64.b64decode(frame.split(b":", 1)[1][:-1], validate=True)
    offset = int.from_bytes(payload[4:8], "little")
    count = int.from_bytes(payload[offset : offset + 2], "little")
    tags = {
        int.from_bytes(payload[pos : pos + 2], "little"): int.from_bytes(
            payload[pos + 8 : pos + 12], "little"
        )
        for pos in range(offset + 2, offset + 2 + count * 12, 12)
    }
    return tags[256], tags[257]


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

    def is_control(self, width, height):
        # The protocol responder uses 8x16 cells. Keep one-row navigation
        # badges and the composer button out of attachment/player assertions.
        y, x = self.screen.cursor.y, self.screen.cursor.x
        return x == self.screen.columns - 7 or (
            height == 16 and width <= 192 and (y == 1 or (self.screen.columns >= 80 and x < 24))
        )

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
                    # Media assertions concern attachment/embed/player pixels;
                    # UI controls have separate image and placement assertions.
                    self.kitty_control = self.is_control(*raster_dimensions("kitty", values))
                    if self.kitty_control:
                        self.control_ids.add(int(values[b"i"]))
                elif self.kitty_header is not None:
                    if data:
                        self.kitty_parts.append(data)
                    if values.get(b"m") == b"0":
                        pixels = base64.b64decode(b"".join(self.kitty_parts), validate=True)
                        channels = int(self.kitty_header[b"f"]) // 8
                        expected = (
                            int(self.kitty_header[b"s"]) * int(self.kitty_header[b"v"]) * channels
                        )
                        if channels not in (3, 4) or not 0 < expected <= 64 * 1024 * 1024:
                            raise AssertionError("Invalid Kitty raster dimensions or format")
                        if self.kitty_header.get(b"o") == b"z":
                            decoder = zlib.decompressobj()
                            pixels = decoder.decompress(pixels, expected + 1)
                            if not decoder.eof or decoder.unused_data:
                                raise AssertionError(
                                    "Incomplete Kitty ZLIB stream or trailing data"
                                )
                        if len(pixels) != expected:
                            raise AssertionError(
                                "Kitty payload does not match its raster dimensions"
                            )
                        # Pixel observers use canonical RGBA, independently of
                        # transport packing. Keep the original wire header.
                        if channels == 3:
                            rgba = bytearray(expected // 3 * 4)
                            rgba[::4], rgba[1::4], rgba[2::4] = (
                                pixels[::3],
                                pixels[1::3],
                                pixels[2::3],
                            )
                            rgba[3::4] = b"\xff" * (expected // 3)
                            pixels = bytes(rgba)
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
                protocol = "iterm2" if iterm else "sixel"
                if not self.include_icons and self.is_control(
                    *raster_dimensions(protocol, sequence)
                ):
                    continue
                self.frames.append(sequence)
                self.frame_positions.append((self.screen.cursor.y, self.screen.cursor.x))
                self.protocols.append(protocol)


class MediaTerminal(unittest.TestCase):
    wait = history_tests.HistoryTerminal.wait

    @classmethod
    def setUpClass(cls):
        from media_assets import load

        load(cls, FFMPEG)

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
            deadline = time.monotonic() + 5
            while query is None and t.process.poll() is None and time.monotonic() < deadline:
                t.read(0.03)
                query = re.search(rb"\x1b_Gi=(\d+),s=1,v=1,a=q", t.output)
            self.assertIsNotNone(
                query,
                f"Kitty query missing; exit={t.process.poll()}, output={bytes(t.output[-1000:])!r}",
            )
            t.send("\x1b_Gi=" + query[1].decode() + ";OK\x1b\\\x1b[?62;22c\x1b[6;16;8t")
        elif protocol == "iterm2":
            t.send("\x1b[?62;22c\x1b[6;16;8t")
        elif sixel:
            t.send("\x1b[?62;4;22c\x1b[6;16;8t")
        t.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(t, "geral")
        t.send("\x1bOB\n")
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
        frame = (
            stream.kitty_frames[index][0]
            if stream.protocols[index] == "kitty"
            else stream.frames[index]
        )
        return raster_dimensions(stream.protocols[index], frame)

    def wait_raster(self, terminal, predicate):
        deadline = time.monotonic() + 10
        while not predicate(*self.raster_size(terminal)) and time.monotonic() < deadline:
            terminal.read(0.03)
        size = self.raster_size(terminal)
        self.assertTrue(predicate(*size), (size, "\n".join(terminal.screen.display)))
        return size

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
