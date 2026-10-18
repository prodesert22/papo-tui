"""PTY/protocol evidence for member panels; not real-emulator visual inspection."""

import base64
import hashlib
import subprocess
from pathlib import Path
import tempfile
from urllib.parse import urlsplit
import unittest

# This import consumes the binary and ffmpeg arguments, as in the media suite.
import test_media_terminal as media
from member_mock import MemberMock, AUTHOR
import test_message_interactions as interactions


class MemberMedia(unittest.TestCase):
    screen = interactions.MessageInteractions.screen
    wait = interactions.MessageInteractions.wait
    settled = interactions.MessageInteractions.settled

    @classmethod
    def setUpClass(cls):
        media.MediaTerminal.setUpClass()

    @classmethod
    def tearDownClass(cls):
        media.MediaTerminal.tearDownClass()

    def fixture(self):
        server = MemberMock()
        image = media.MediaTerminal.large_images["landscape"]
        server.avatar = base64.b64encode(media.MediaTerminal.images["png"]).decode()
        server.banner = hashlib.sha256(image).hexdigest()
        server.messages = server.messages[:2]
        for message in server.messages:
            message.update(
                author_id=AUTHOR,
                attachments=[
                    dict(
                        id=media.uuid(901),
                        mime_type="image/png",
                        original_file_name="History picture",
                        size_bytes=len(image),
                        moderation_status="clean",
                    )
                ],
            )
        server.messages[0]["content"] += " ☕"
        server.messages[1]["attachments"] = [
            dict(
                id=media.uuid(902),
                mime_type="video/mp4",
                original_file_name="History video",
                size_bytes=len(media.MediaTerminal.video),
                moderation_status="clean",
            )
        ]
        parent = server.api_handler

        def route(method, path, body):
            if urlsplit(path).path.endswith("/media/" + server.banner) and getattr(
                server, "banner_status", None
            ):
                return server.banner_status, None
            if "/attachments/" + media.uuid(902) in path:
                return 200, media.MediaTerminal.video, None, None, "video/mp4"
            if "/attachments/" in path or urlsplit(path).path.endswith("/media/" + server.banner):
                return 200, image, None, None, "image/png"
            return parent(method, path, body)

        server.api_handler = route
        return server

    @staticmethod
    def placement_dimensions(header):
        # Unscaled Kitty placements use c=0/r=0 and their explicit pixel size.
        columns = int(header.get(b"c", b"0")) or (int(header[b"w"]) + 7) // 8
        rows = int(header.get(b"r", b"0")) or (int(header[b"h"]) + 15) // 16
        return columns, rows

    def current_history_placements(self, terminal, left):
        current = {}
        stream = terminal.stream
        for header, (y, x) in zip(stream.placements, stream.placement_positions):
            key = (int(header[b"i"]), int(header[b"p"]))
            current[key] = (header, y, x)
        return [
            (header, y, x)
            for key, (header, y, x) in current.items()
            if key in stream.live_placements
            and y >= 8
            and 27 <= x < left
            and self.placement_dimensions(header)[0] > 6
        ]

    def test_drawer_menu_profile_resize_and_close_preserve_uncovered_media(self):
        for protocol in ("kitty", "sixel", "iterm2"):
            with self.subTest(protocol=protocol), self.fixture() as server:
                helper = media.MediaTerminal()
                helper.include_icons = True
                terminal = helper.open(server, protocol=protocol, size=(44, 180))
                try:
                    helper.wait_frames(terminal)
                    terminal.send("\x1b[21~")
                    self.wait(terminal, "Membros (6)")
                    self.wait(terminal, "Equipe")
                    interactions.MessageInteractions.click(self, terminal, "Conversar em #geral")
                    terminal.send("\x1bOS")
                    self.wait(terminal, "Emojis")
                    terminal.resize(42, 170)
                    self.wait(terminal, "Emojis")
                    terminal.send("\x1b")
                    terminal.resize(44, 180)
                    terminal.send("\x1b[21~\x1b[21~")
                    self.wait(terminal, "Membros (6)")
                    terminal.send("/bob\n")
                    self.wait(terminal, "Membros (1)")
                    helper.wait_frames(terminal, 2)
                    terminal.send("m")
                    self.wait(terminal, "Bloquear")
                    self.assertTrue(any(x < 100 for y, x in terminal.stream.frame_positions))
                    terminal.send("\x1b\n")
                    self.wait(terminal, "@bob")
                    self.settled(
                        terminal,
                        lambda: any("/media/" in path for method, path, *_ in server.requests),
                    )
                    # Check current live occurrences, rather than a historical frame.
                    if protocol == "kitty":
                        self.settled(
                            terminal, lambda: bool(self.current_history_placements(terminal, 58))
                        )
                        for header, y, x in self.current_history_placements(terminal, 58):
                            columns, rows = self.placement_dimensions(header)
                            self.assertTrue(
                                x + columns <= 58 or y + rows <= 8 or y >= 36,
                                "History pixels must not overlap the profile",
                            )
                    else:
                        self.settled(terminal, lambda: len(terminal.stream.frames) >= 3)
                    before = sum("/attachments/" in path for method, path, *_ in server.requests)
                    terminal.resize(38, 130)
                    self.wait(terminal, "@bob")
                    terminal.send("\x1b\x1b[21~")
                    self.wait(terminal, "Conversar em #geral")
                    interactions.MessageInteractions.click(self, terminal, "[DMs]")
                    self.wait(terminal, "Bia ·")
                    frames = len(terminal.stream.frame_positions)
                    reads = sum("/messages" in path for _, path, *_ in server.requests)
                    terminal.send("m")
                    self.wait(terminal, "Fechar DM")
                    menu_row = next(
                        i for i, line in enumerate(terminal.screen.display) if "Fechar DM" in line
                    )
                    menu_left = terminal.screen.display[menu_row].index("Fechar DM") - 1
                    if protocol == "kitty":
                        self.settled(
                            terminal, lambda: bool(self.current_history_placements(terminal, 129))
                        )
                        for header, y, x in self.current_history_placements(terminal, 129):
                            columns, rows = self.placement_dimensions(header)
                            self.assertTrue(
                                x + columns <= menu_left
                                or x >= menu_left + 30
                                or y + rows <= menu_row - 1
                                or y >= menu_row + 2,
                                "History pixels must not overlap the DM menu",
                            )
                    else:
                        self.settled(
                            terminal,
                            lambda: any(
                                y >= 8 and x >= 27 and (y >= menu_row + 2 or x >= menu_left + 30)
                                for y, x in terminal.stream.frame_positions[frames:]
                            ),
                        )
                    terminal.resize(36, 120)
                    self.wait(terminal, "Fechar DM")
                    terminal.send("\x1b")
                    self.settled(terminal, lambda: "Fechar DM" not in self.screen(terminal))
                    self.assertEqual(
                        sum("/messages" in path for _, path, *_ in server.requests), reads
                    )
                    self.assertEqual(
                        before, sum("/attachments/" in path for method, path, *_ in server.requests)
                    )
                    self.assertEqual(
                        sum("/media/" in path for method, path, *_ in server.requests), 1
                    )
                finally:
                    self.assertTrue(terminal.close())
                    helper.doCleanups()
                    helper.tearDown()

    def test_banner_failure_retries_only_after_explicit_profile_refresh(self):
        with self.fixture() as server:
            server.banner_status = 503
            helper = media.MediaTerminal()
            terminal = helper.open(server, protocol="kitty", size=(40, 150))
            try:
                terminal.send("\x1b[21~/bob\n")
                self.wait(terminal, "Membros (1)")
                terminal.send("\n")
                self.wait(terminal, "@bob")
                self.settled(
                    terminal, lambda: sum("/media/" in path for _, path, *_ in server.requests) == 1
                )
                self.wait(terminal, "Banner indisponível")
                server.banner_status = None
                terminal.send("\x12")
                self.settled(
                    terminal, lambda: sum("/media/" in path for _, path, *_ in server.requests) == 2
                )
                self.wait(terminal, "@bob")
            finally:
                self.assertTrue(terminal.close())
                helper.doCleanups()
                helper.tearDown()

    def test_drawer_avatars_remain_square_with_tall_terminal_cells(self):
        for protocol in ("kitty", "sixel", "iterm2"):
            with self.subTest(protocol=protocol), self.fixture() as server:
                helper = media.MediaTerminal()
                helper.include_icons = True
                terminal = helper.open(server, protocol=protocol, size=(44, 180))
                try:
                    terminal.send("\x1b[6;25;11t\x1b[21~/bob\n")
                    self.wait(terminal, "Membros (1)")

                    def avatars():
                        return [
                            i
                            for i, (y, x) in enumerate(terminal.stream.frame_positions)
                            if x == 151 and y > 6
                        ]

                    self.settled(terminal, lambda: bool(avatars()))
                    index = avatars()[-1]
                    frame = (
                        terminal.stream.kitty_frames[index][0]
                        if protocol == "kitty"
                        else terminal.stream.frames[index]
                    )
                    width, height = media.raster_dimensions(protocol, frame)
                    self.assertLessEqual(width, 33)
                    # Kitty/iTerm2 pad the canvas to full terminal cells. The
                    # 4:3 red avatar must keep its full aspect inside that canvas.
                    if protocol == "sixel":
                        self.assertEqual(width, height)
                    else:
                        if protocol == "kitty":
                            pixels = terminal.stream.kitty_frames[index][1]
                            stride = 4
                        else:
                            payload = base64.b64decode(frame.split(b":", 1)[1][:-1])
                            with tempfile.TemporaryDirectory(prefix="papo-avatar-iterm-") as folder:
                                image = Path(folder, "avatar.tiff")
                                image.write_bytes(payload)
                                decoded = subprocess.run(
                                    [
                                        media.FFMPEG,
                                        "-v",
                                        "error",
                                        "-i",
                                        str(image),
                                        "-f",
                                        "rawvideo",
                                        "-pix_fmt",
                                        "rgb24",
                                        "-threads",
                                        "1",
                                        "pipe:1",
                                    ],
                                    capture_output=True,
                                    check=False,
                                )
                            self.assertEqual(decoded.returncode, 0, decoded.stderr.decode())
                            pixels = decoded.stdout
                            stride = 3
                        red = [
                            (i // stride % width, i // stride // width)
                            for i in range(0, len(pixels), stride)
                            if pixels[i] > 200 and pixels[i + 1] < 40 and pixels[i + 2] < 40
                        ]
                        self.assertTrue(red)
                        red_width = max(x for x, _ in red) - min(x for x, _ in red) + 1
                        red_height = max(y for _, y in red) - min(y for _, y in red) + 1
                        self.assertAlmostEqual(red_width / red_height, 4 / 3, delta=0.12)
                        self.assertLessEqual(red_height, 33)
                    terminal.resize(36, 130)
                    self.wait(terminal, "Membros (1)")
                    terminal.send("\x1b[21~")
                    self.wait(terminal, "Conversar em #geral")
                finally:
                    self.assertTrue(terminal.close())
                    helper.doCleanups()
                    helper.tearDown()

    def test_light_and_ultralight_member_images_follow_application_mode(self):
        for mode in ("light", "ultralight"):
            with self.subTest(mode=mode), self.fixture() as server:
                helper = media.MediaTerminal()
                terminal = helper.open(server, sixel=False, size=(36, 130))
                try:
                    terminal.send("\x1bOQ")
                    self.wait(terminal, "Configurações")
                    terminal.send("\t" + "\x1bOB" * (1 if mode == "light" else 2) + "\n\t\n")
                    self.settled(terminal, lambda: "Configurações" not in self.screen(terminal))
                    baseline = len(server.requests)
                    terminal.send("\x1b[21~/bob\n")
                    self.wait(terminal, "Membros (1)")
                    terminal.send("\n")
                    self.wait(terminal, "@bob")
                    if mode == "light":
                        self.settled(
                            terminal,
                            lambda: any(
                                "/media/" in path for _, path, *_ in server.requests[baseline:]
                            ),
                        )
                    else:
                        self.assertFalse(
                            any(
                                "/media/" in path or "/profile_batch" in path
                                for _, path, *_ in server.requests[baseline:]
                            )
                        )
                    self.assertEqual(terminal.stream.frames, [])
                finally:
                    self.assertTrue(terminal.close())
                    helper.doCleanups()
                    helper.tearDown()


if __name__ == "__main__":
    unittest.main()
