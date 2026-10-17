"""App modes through a PTY and synthetic HTTP; never uses personal configuration."""

import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

BINARY, FFMPEG = map(Path, sys.argv[1:3])
sys.argv = [sys.argv[0], str(BINARY), str(FFMPEG), *sys.argv[3:]]
import test_media_terminal as media  # noqa: E402 - prepare test arguments before import
import test_terminal  # noqa: E402
import test_emojis_terminal as emoji_tests  # noqa: E402
from history_mock import uuid  # noqa: E402
from interaction_mock import InteractionMock  # noqa: E402


class AppModes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        media.MediaTerminal.setUpClass()

    @classmethod
    def tearDownClass(cls):
        media.MediaTerminal.tearDownClass()

    def setUp(self):
        self.helper = media.MediaTerminal()
        self.root = tempfile.TemporaryDirectory(prefix="papo-modes-")
        self.addCleanup(self.root.cleanup)
        self.config = Path(self.root.name, "papo-tui", "config.json")
        self.environment = {
            "XDG_CONFIG_HOME": self.root.name,
            "XDG_STATE_HOME": str(Path(self.root.name, "state")),
        }
        self.previous = test_terminal.BINARY
        test_terminal.BINARY = str(BINARY.resolve())
        self.addCleanup(setattr, test_terminal, "BINARY", self.previous)
        self.addCleanup(self.helper.doCleanups)

    def seed(self, mode, version=4):
        self.config.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        data = dict(version=version, profiles=[], selected_profile=None)
        if version >= 2:
            data.update(requests_per_second=10, request_burst=25)
        if version >= 3:
            data.update(desktop_notifications=False, notification_preview=False)
        if version == 4:
            data["app_mode"] = mode
        self.config.write_text(json.dumps(data))
        self.config.chmod(0o600)

    def open(self, server, **kwargs):
        shutil.rmtree(Path(self.root.name, "state"), ignore_errors=True)
        return self.helper.open(server, sixel=False, environment=self.environment, **kwargs)

    def wait(self, t, predicate, timeout=8):
        deadline = time.monotonic() + timeout
        while not predicate() and time.monotonic() < deadline:
            t.read(0.03)
        self.assertTrue(predicate(), "\n".join(t.screen.display))

    def choose(self, t, mode):
        t.send("\x1bOQ")
        self.helper.wait(t, "Configurações")
        current = (
            json.loads(self.config.read_text()).get("app_mode", "normal")
            if self.config.exists()
            else "normal"
        )
        selected = ["normal", "light", "ultralight"].index(current)
        wanted = ["normal", "light", "ultralight"].index(mode)
        t.send(
            "\t"
            + (
                "\x1bOB" * (wanted - selected)
                if wanted >= selected
                else "\x1bOA" * (selected - wanted)
            )
            + "\n\t\n"
        )
        self.wait(
            t,
            lambda: (
                self.config.exists() and json.loads(self.config.read_text()).get("app_mode") == mode
            ),
        )

    def children(self, t):
        found = []
        for task in Path(f"/proc/{t.process.pid}/task").iterdir():
            try:
                pids = (task / "children").read_text().split()
            except FileNotFoundError:
                continue
            for pid in pids:
                try:
                    if b"papo-media-worker" in Path(f"/proc/{pid}/cmdline").read_bytes():
                        found.append(int(pid))
                except FileNotFoundError:
                    pass
        return list(set(found))

    def assert_no_media_libs(self, t):
        maps = Path(f"/proc/{t.process.pid}/maps").read_text()
        for name in ("libchafa.", "libsixel.", "libpango", "libcairo.", "libavcodec.", "libmpv."):
            self.assertNotIn(name, maps)

    def test_ultralight_lists_each_attachment_without_download_or_gutter(self):
        self.seed("ultralight")
        with self.helper.serve(media.MediaTerminal.images["png"], "image/png") as server:
            server.messages[0]["attachments"] = [
                dict(id=uuid(901 + i), mime_type=mime, original_file_name=name, size_bytes=100)
                for i, (mime, name) in enumerate(
                    (
                        ("image/png", "foto.png"),
                        ("image/gif", "anim.gif"),
                        ("video/mp4", "filme.mp4"),
                    )
                )
            ]
            t = self.open(server)
            try:
                self.helper.wait(t, "filme.mp4")
                rows = [
                    (i, row)
                    for i, row in enumerate(t.screen.display)
                    if any(name in row for name in ("foto.png", "anim.gif", "filme.mp4"))
                ]
                self.assertEqual(len(rows), 3)
                self.assertEqual([y for y, _ in rows], list(range(rows[0][0], rows[0][0] + 3)))
                self.assertEqual(
                    [
                        row.index(label)
                        for (_, row), label in zip(rows, ("Imagem:", "GIF:", "Vídeo:"))
                    ],
                    [28] * 3,
                )
                self.assertFalse(
                    any(
                        "/attachments/" in req[1] or "/embeds/" in req[1] for req in server.requests
                    )
                )
                self.assertEqual(self.children(t), [])
                self.assert_no_media_libs(t)
                self.assertNotIn(b"\x1b_G", t.output)
            finally:
                self.assertTrue(t.close())

    def test_light_and_normal_fallback_animate_cells_without_graphics_protocols(self):
        for mode in ("light", "normal"):
            with self.subTest(mode=mode):
                self.seed(mode)
                with self.helper.serve(media.MediaTerminal.animated_gif, "image/gif") as server:
                    t = self.open(server)
                    try:
                        self.wait(
                            t, lambda: any("/attachments/" in req[1] for req in server.requests)
                        )
                        t.read(0.6)
                        frames = set()
                        seen_colors = set()
                        deadline = time.monotonic() + 5
                        while (
                            len(frames) < 2 or not {"ff0000", "0000ff"} <= seen_colors
                        ) and time.monotonic() < deadline:
                            t.read(0.08)
                            frames.add(
                                tuple(
                                    (
                                        t.screen.buffer[y][x].data,
                                        t.screen.buffer[y][x].fg,
                                        t.screen.buffer[y][x].bg,
                                    )
                                    for y in range(8, 16)
                                    for x in range(32, 75)
                                )
                            )
                            seen_colors.update(
                                color
                                for y in range(8, 16)
                                for x in range(32, 75)
                                for color in (t.screen.buffer[y][x].fg, t.screen.buffer[y][x].bg)
                            )
                        self.assertTrue({"ff0000", "0000ff"} <= seen_colors)
                        self.assertGreaterEqual(len(frames), 2)
                        self.assertEqual(t.stream.frames, [])
                        self.assert_no_media_libs(t)
                        self.assertTrue(self.children(t))
                        t.send("rascunho")
                        self.helper.wait(t, "rascunho")
                    finally:
                        self.assertTrue(t.close())

    def test_custom_emoji_freezes_in_light_while_identical_attachment_keeps_animating(self):
        for format in ("GIF", "WEBP", "PNG"):
            with self.subTest(format=format):
                self.check_custom_emoji_mode_transitions(format)

    def check_custom_emoji_mode_transitions(self, format):
        self.seed("normal")
        emojis = emoji_tests.EmojiTerminal()
        emojis.png = media.MediaTerminal.images["png"]
        emojis.gif = emoji_tests.encode_emoji_animation(media.MediaTerminal.animated_gif, format)
        with emojis.server(content="Emoji :gato 界: fim", animated=True) as server:
            server.emojis[0]["format"] = format
            server.messages[0]["attachments"] = [
                dict(
                    id=uuid(901),
                    mime_type={"GIF": "image/gif", "WEBP": "image/webp", "PNG": "image/png"}[
                        format
                    ],
                    original_file_name="same." + format.lower(),
                    size_bytes=len(emojis.gif),
                    moderation_status="clean",
                )
            ]
            parent = server.api_handler

            def route(method, path, body):
                if "/attachments/" in path:
                    return (
                        200,
                        emojis.gif,
                        None,
                        None,
                        server.messages[0]["attachments"][0]["mime_type"],
                    )
                return parent(method, path, body)

            server.api_handler = route
            t = self.open(server, size=(40, 100))
            try:
                for mode in ("normal", "light", "normal", "ultralight"):
                    with self.subTest(mode=mode):
                        if json.loads(self.config.read_text())["app_mode"] != mode:
                            self.choose(t, mode)
                        self.helper.wait(t, "Emoji ")
                        if mode == "ultralight":
                            self.helper.wait(t, ":gato 界:")
                            self.wait(t, lambda: not self.children(t))
                            continue

                        def emoji_cells():
                            row = next(
                                i for i, line in enumerate(t.screen.display) if "Emoji " in line
                            )
                            column = t.screen.display[row].index("Emoji ") + len("Emoji ")
                            return row, [t.screen.buffer[row][x] for x in (column, column + 1)]

                        def tone(color):
                            if len(color) != 6:
                                return None
                            try:
                                red, green, blue = bytes.fromhex(color)
                            except ValueError:
                                return None
                            if red > 150 and green < 30 and blue < 30:
                                return "red"
                            if blue > 150 and red < 30 and green < 30:
                                return "blue"
                            return None

                        self.wait(
                            t,
                            lambda: any(
                                tone(color) is not None
                                for cell in emoji_cells()[1]
                                for color in (cell.fg, cell.bg)
                            ),
                        )
                        emoji_frames, attachment_colors = set(), set()
                        deadline = time.monotonic() + 1.2
                        while time.monotonic() < deadline:
                            t.read(0.05)
                            row, cells = emoji_cells()
                            emoji_frames.add(tuple((cell.data, cell.fg, cell.bg) for cell in cells))
                            attachment_colors.update(
                                tone(color)
                                for y in range(row + 1, min(row + 8, t.screen.lines - 8))
                                for x in range(34, 75)
                                for color in (t.screen.buffer[y][x].fg, t.screen.buffer[y][x].bg)
                            )
                        self.assertTrue({"red", "blue"} <= attachment_colors)
                        if mode == "light":
                            self.assertEqual(len(emoji_frames), 1)
                            self.assertNotIn(
                                "blue",
                                {tone(color) for cell in cells for color in (cell.fg, cell.bg)},
                            )
                        else:
                            self.assertGreaterEqual(len(emoji_frames), 2)
                        self.assertEqual(t.stream.frames, [])
                self.assertEqual(server.message_count, 0)
            finally:
                self.assertTrue(t.close())

    def test_emoji_only_messages_enlarge_in_chafa_and_light_and_remain_text_in_ultralight(self):
        for mode in ("normal", "light", "ultralight"):
            with self.subTest(mode=mode):
                self.seed(mode)
                emojis = emoji_tests.EmojiTerminal()
                emojis.png = media.MediaTerminal.images["png"]
                emojis.gif = media.MediaTerminal.animated_gif
                with emojis.server(content=":gato 界:", count=2, animated=True) as server:
                    for message in server.messages:
                        message["author_id"] = uuid(21)
                    server.messages[1]["content"] = "Depois"
                    t = self.open(server, size=(40, 100))
                    try:
                        self.helper.wait(t, "Depois")
                        if mode == "ultralight":
                            self.helper.wait(t, ":gato 界:")
                            self.assertFalse(any("/emojis?" in r[1] for r in server.requests))
                            self.assertFalse(self.children(t))
                            continue

                        def cells():
                            end = next(
                                (i for i, line in enumerate(t.screen.display) if "Depois" in line),
                                -1,
                            )
                            if end < 3:
                                return []
                            return [
                                t.screen.buffer[y][x]
                                for y in range(end - 3, end)
                                for x in range(34, 40)
                            ]

                        def colored():
                            return sum(
                                any(color in (cell.fg, cell.bg) for color in ("ff0000", "0000ff"))
                                for cell in cells()
                            )

                        # Wait for the whole enlarged red/blue fixture. Loading
                        # labels and indexed theme backgrounds are not frames.
                        self.wait(t, lambda: colored() == 18)
                        frames = set()
                        deadline = time.monotonic() + 0.9
                        while time.monotonic() < deadline:
                            t.read(0.05)
                            frames.add(tuple((cell.data, cell.fg, cell.bg) for cell in cells()))
                        if mode == "light":
                            self.assertEqual(len(frames), 1)
                            self.assertTrue(all("ff0000" in (c.fg, c.bg) for c in cells()))
                        else:
                            self.assertGreater(len(frames), 1)
                        self.assertEqual(t.stream.frames, [])
                    finally:
                        self.assertTrue(t.close())

    def test_chafa_preserves_image_colors_on_indexed_and_direct_terminals(self):
        colors = ("ff5f00", "00d7af", "875fff")
        pixels = b"".join(bytes.fromhex(color) * 32 for color in colors) * 32
        encoded = subprocess.run(
            [
                str(FFMPEG),
                "-v",
                "error",
                "-f",
                "rawvideo",
                "-pixel_format",
                "rgb24",
                "-video_size",
                "96x32",
                "-i",
                "pipe:0",
                "-frames:v",
                "1",
                "-threads",
                "1",
                "-f",
                "image2pipe",
                "-vcodec",
                "png",
                "pipe:1",
            ],
            input=pixels,
            capture_output=True,
            check=True,
        ).stdout
        for term in ("xterm-256color", "xterm-direct"):
            for mode in ("light", "normal"):
                with self.subTest(term=term, mode=mode):
                    self.seed(mode)
                    self.environment["TERM"] = term
                    with self.helper.serve(encoded, "image/png") as server:
                        t = self.open(server)
                        try:

                            def image_colors():
                                return {
                                    color
                                    for y in range(7, 17)
                                    for x in range(32, 76)
                                    for color in (
                                        t.screen.buffer[y][x].fg,
                                        t.screen.buffer[y][x].bg,
                                    )
                                }

                            self.wait(t, lambda: set(colors) <= image_colors())
                            before = image_colors()
                            t.send("keep colors")
                            self.helper.wait(t, "keep colors")
                            self.assertTrue(set(colors) <= image_colors(), before)
                            t.resize(28, 100)
                            self.wait(t, lambda: set(colors) <= image_colors())
                        finally:
                            self.assertTrue(t.close())

    def test_settings_up_returns_from_both_buttons_and_placeholder_sections(self):
        self.seed("normal")
        with self.helper.serve(media.MediaTerminal.images["png"], "image/png") as server:
            t = self.open(server)
            try:
                t.send("draft remains")
                t.send("\x1bOQ\t\x1bOB\x1bOB\t\x1bOC\x1bOA\x1bOA\n\t\n")
                self.wait(t, lambda: json.loads(self.config.read_text())["app_mode"] == "light")
                self.helper.wait(t, "draft remains")
                # General: Up from Save returns to the highlighted mode.
                t.send("\x1bOQ\t\t\x1bOA\x1bOA\n\t\n")
                self.wait(t, lambda: json.loads(self.config.read_text())["app_mode"] == "normal")
                # Placeholder: Up from Cancel returns to the section selector.
                t.send("\x1bOQ\x1bOC\n\x1bOC\x1bOA\x1bOD")
                self.helper.wait(t, "Ultralight")
                t.send("\x1b")
            finally:
                self.assertTrue(t.close())

    def test_light_video_never_downloads_or_loads_player(self):
        self.seed("light")
        with self.helper.serve(media.MediaTerminal.video, "video/mp4") as server:
            t = self.open(server)
            try:
                self.helper.wait(t, "Fixture")
                t.send("\x1b[18~\n")
                self.helper.wait(t, "Vídeos desativados")
                self.assertFalse(any("/attachments/" in req[1] for req in server.requests))
                for pid in self.children(t):
                    self.assertNotIn("libmpv.", Path(f"/proc/{pid}/maps").read_text())
            finally:
                self.assertTrue(t.close())

    def test_message_actions_and_reply_drafts_survive_settings_and_mode_changes(self):
        for mode, destination in (
            ("normal", "light"),
            ("light", "ultralight"),
            ("ultralight", "normal"),
        ):
            with self.subTest(mode=mode):
                self.seed(mode)
                with InteractionMock(count=1) as server:
                    target = server.messages[0]["id"]
                    server.seed(target, {"unicode": "👍"}, [uuid(30)])
                    t = self.open(server, size=(32, 100))
                    try:
                        self.helper.wait(t, "Mensagem 0001")
                        t.send("draft stays\x1b[18~E")
                        self.helper.wait(t, "Quem reagiu")
                        self.helper.wait(t, "Pessoas")
                        self.wait(
                            t,
                            lambda: any(
                                r[1].split("?")[0].endswith("/reactions") for r in server.requests
                            ),
                        )
                        t.send("\x1bOQ")
                        self.helper.wait(t, "Configurações")
                        self.assertNotIn("Quem reagiu", "\n".join(t.screen.display))
                        t.send("\x1br")
                        self.helper.wait(t, "Respondendo a")
                        t.send("\x1b")
                        self.assertNotIn("Respondendo a", "\n".join(t.screen.display))
                        t.send(" tail")
                        self.helper.wait(t, "draft stays tail")
                        t.send("\x1b[18~e")
                        self.helper.wait(t, "Reagir · Esc voltar")
                        self.choose(t, destination)
                        self.assertNotIn("Reagir · Esc voltar", "\n".join(t.screen.display))
                        t.send("r\n")
                        self.helper.wait(t, "Mensagem confirmada")
                        self.assertEqual(server.sent_messages[0]["content"], "draft stays tail")
                        self.assertEqual(server.sent_messages[0]["reply_to"], target)
                        self.assertEqual(server.reaction_calls, [])
                        self.assert_no_media_libs(t)
                        if destination == "ultralight":
                            self.wait(t, lambda: not self.children(t))
                    finally:
                        self.assertTrue(t.close())

    def test_chafa_keeps_uncovered_chat_pixels_behind_floating_panels(self):
        image = subprocess.run(
            [
                str(FFMPEG),
                "-v",
                "error",
                "-f",
                "lavfi",
                "-i",
                "color=red:s=1000x500",
                "-frames:v",
                "1",
                "-threads",
                "1",
                "-f",
                "image2pipe",
                "-vcodec",
                "png",
                "pipe:1",
            ],
            check=True,
            capture_output=True,
        ).stdout
        for mode in ("normal", "light"):
            for key, title in (
                ("\x1bOR", "Anexar arquivos"),
                ("attachment-remove", "Remover anexo?"),
                ("\x1bOS", "Emojis · Esc voltar"),
                ("E", "Quem reagiu"),
                ("@", "Menções"),
                ("\x1b[20~", "Notificações"),
            ):
                with self.subTest(mode=mode, modal=title):
                    self.seed(mode)
                    with self.helper.serve(image, "image/png") as server:
                        parent = server.api_handler

                        def route(method, path, body):
                            if method == "GET" and path.split("?")[0].endswith("/reactions"):
                                return 200, dict(
                                    message_id=server.messages[0]["id"],
                                    reactions=[],
                                    has_more=False,
                                )
                            return parent(method, path, body)

                        server.api_handler = route
                        t = self.open(server, size=(48, 160))
                        try:

                            def red_cells():
                                return {
                                    (y, x)
                                    for y in range(8, 40)
                                    for x in range(32, 150)
                                    if "ff0000"
                                    in (t.screen.buffer[y][x].fg, t.screen.buffer[y][x].bg)
                                }

                            self.wait(t, lambda: len(red_cells()) > 200)
                            if key == "E":
                                t.send("\x1b[18~")
                                self.wait(t, lambda: len(red_cells()) > 200)
                            if key == "attachment-remove":
                                t.send("\x1bOR")
                                self.helper.wait(t, "Anexar arquivos")
                                t.send(
                                    "\x03"
                                    + str(Path(self.helper.assets.name) / "landscape.png")
                                    + "\n"
                                )
                                self.helper.wait(t, "landscape.png 🗙]")
                                self.wait(t, lambda: len(red_cells()) > 200)
                            before = red_cells()
                            t.send("\x1b[Z\n" if key == "attachment-remove" else key)
                            self.helper.wait(t, title)
                            top = next(
                                i for i, line in enumerate(t.screen.display) if title in line
                            )
                            title_x = t.screen.display[top].index(title)
                            if title in ("Notificações", "Remover anexo?"):
                                top -= 1
                            self.wait(
                                t,
                                lambda: any(
                                    t.screen.buffer[top][x].data in ("k", "┐")
                                    for x in range(title_x, t.screen.columns)
                                ),
                            )
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

                            def panel_bottom():
                                return next(
                                    (
                                        y
                                        for y in range(top + 1, t.screen.lines)
                                        if t.screen.buffer[y][left].data in ("m", "└")
                                        and t.screen.buffer[y][right - 1].data in ("j", "┘")
                                    ),
                                    None,
                                )

                            self.wait(t, lambda: panel_bottom() is not None)
                            bottom = panel_bottom() + 1
                            exposed = {
                                (y, x)
                                for y, x in before
                                if y < top or y >= bottom or x < left or x >= right
                            }
                            self.assertGreater(len(exposed), 8)
                            self.wait(t, lambda: exposed <= red_cells())
                            self.assertEqual(t.stream.frames, [])
                            if title in (
                                "Menções",
                                "Notificações",
                                "Anexar arquivos",
                                "Remover anexo?",
                            ):
                                active_title = title
                                if title == "Notificações":
                                    t.send("\x1bOC\ngeral")
                                    active_title = "Selecionar canal"
                                    self.helper.wait(t, active_title)
                                    self.wait(t, lambda: exposed <= red_cells())
                                t.send("\x12")
                                self.helper.wait(t, active_title)
                                self.wait(t, lambda: exposed <= red_cells())
                                t.resize(44, 144)
                                self.helper.wait(t, active_title)
                                self.wait(t, lambda: len(red_cells()) > 200)
                                t.send("\x1b")
                                self.assertNotIn(active_title, "\n".join(t.screen.display))
                                if title == "Notificações":
                                    self.helper.wait(t, title)
                                    t.send("\x1b")
                                self.wait(t, lambda: len(red_cells()) > 200)
                        finally:
                            self.assertTrue(t.close())

    def test_ultralight_attachment_panels_never_start_media(self):
        self.seed("ultralight")
        with self.helper.serve(media.MediaTerminal.images["png"], "image/png") as server:
            source = Path(self.root.name, "attachment.txt")
            source.write_text("Synthetic local attachment")
            t = self.open(server, size=(48, 160))
            try:
                self.helper.wait(t, "Fixture")
                t.send("\x1bOR")
                self.helper.wait(t, "Anexar arquivos")
                t.send("\x03" + str(source) + "\n")
                self.helper.wait(t, "attachment.txt 🗙]")
                t.send("\x1b[Z\n")
                self.helper.wait(t, "Remover anexo?")
                t.resize(24, 80)
                self.helper.wait(t, "Remover anexo?")
                t.send("\x1b")
                self.helper.wait(t, "attachment.txt 🗙]")
                self.assertEqual(self.children(t), [])
                self.assertEqual(t.stream.frames, [])
                self.assertFalse(any("/attachments/" in request[1] for request in server.requests))
            finally:
                self.assertTrue(t.close())

    def test_text_modes_do_not_initialize_download_directories(self):
        for mode in ("ultralight", "light"):
            with self.subTest(mode=mode):
                self.seed(mode)
                self.environment["TMPDIR"] = self.root.name
                with self.helper.serve(media.MediaTerminal.images["png"], "image/png") as server:
                    server.messages[0]["attachments"] = []
                    t = self.open(server)
                    try:
                        self.helper.wait(t, "Mensagem 0001")
                        t.read(0.3)
                        self.assertEqual(list(Path(self.root.name).glob("papo-media-*")), [])
                        self.assertEqual(self.children(t), [])
                        t.send("text still works")
                        self.helper.wait(t, "text still works")
                    finally:
                        self.assertTrue(t.close())

    def test_light_idle_worker_releases_codecs_and_restarts_for_modal(self):
        self.seed("light")
        with self.helper.serve(media.MediaTerminal.images["png"], "image/png") as server:
            t = self.open(server)
            try:
                self.wait(t, lambda: bool(self.children(t)))

                def red_cells():
                    return any(
                        "ff0000" in (t.screen.buffer[y][x].fg, t.screen.buffer[y][x].bg)
                        for y in range(8, 16)
                        for x in range(32, 75)
                    )

                self.wait(t, red_cells)
                self.wait(t, lambda: not self.children(t), timeout=6)
                requests = sum("/attachments/" in r[1] for r in server.requests)
                t.send("cached image stays")
                self.helper.wait(t, "cached image stays")
                self.assertTrue(red_cells())
                self.assertEqual(self.children(t), [])
                t.send("\x1b")
                t.send("\x1b[18~\n")
                self.helper.wait(t, "[Info]")
                self.wait(t, lambda: bool(self.children(t)))
                self.assertEqual(sum("/attachments/" in r[1] for r in server.requests), requests)
            finally:
                self.assertTrue(t.close())

    def test_normal_video_uses_chafa_without_graphics_support(self):
        self.seed("normal")
        with self.helper.serve(media.MediaTerminal.video, "video/mp4") as server:
            t = self.open(server)
            try:
                self.helper.wait(t, "Fixture")
                t.send("rascunho do vídeo\x1b\x1b[18~\n")
                self.helper.wait(t, "Reproduzindo")
                self.assertEqual(t.stream.frames, [])
                self.assertTrue(
                    any(
                        "libmpv." in Path(f"/proc/{pid}/maps").read_text()
                        for pid in self.children(t)
                    )
                )
                t.send(" ")
                self.helper.wait(t, "Pausado")
                t.send("\x1b")
                self.helper.wait(t, "rascunho do vídeo")
            finally:
                self.assertTrue(t.close())

    def test_hot_switch_retires_helper_and_preserves_draft_then_restarts(self):
        self.seed("normal")
        with self.helper.serve(media.MediaTerminal.animated_gif, "image/gif") as server:
            t = self.open(server)
            try:
                self.wait(t, lambda: bool(self.children(t)))
                t.send("rascunho preservado")
                self.choose(t, "ultralight")
                self.helper.wait(t, "rascunho preservado")
                self.wait(t, lambda: not self.children(t), timeout=3)
                self.assert_no_media_libs(t)
                count = len([req for req in server.requests if "/attachments/" in req[1]])
                t.read(0.5)
                self.assertEqual(
                    count, len([req for req in server.requests if "/attachments/" in req[1]])
                )
                self.choose(t, "light")
                self.wait(t, lambda: bool(self.children(t)))
                self.helper.wait(t, "rascunho preservado")
            finally:
                self.assertTrue(t.close())
            restarted = self.open(server)
            try:
                self.assertEqual(json.loads(self.config.read_text())["app_mode"], "light")
                self.assertNotIn(b"\x1b_G", restarted.output)
            finally:
                self.assertTrue(restarted.close())

    def test_gear_cancel_placeholders_and_legacy_migration(self):
        self.seed("normal", version=1)
        with self.helper.serve(media.MediaTerminal.images["png"], "image/png") as server:
            t = self.open(server)
            try:
                t.click(19, 21)
                self.helper.wait(t, "Configurações")
                t.send("\x1bOC")
                self.helper.wait(t, "Em breve")
                t.resize(22, 44)
                self.helper.wait(t, "Configurações")
                t.send("\x1b")
                self.assertEqual(json.loads(self.config.read_text())["version"], 1)
                t.resize(24, 80)
                self.choose(t, "ultralight")
                self.assertEqual(json.loads(self.config.read_text())["version"], 4)
            finally:
                self.assertTrue(t.close())

    def test_unresponsive_worker_is_reaped_after_switch(self):
        self.seed("normal")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            shutil.copy2(BINARY, root / "papo-tui")
            (root / "papo-backends").mkdir()
            worker = root / "papo-backends" / "papo-media-worker"
            worker.write_text(
                "#!/usr/bin/python3\nimport signal,time\nsignal.signal(signal.SIGTERM,signal.SIG_IGN)\nwhile True: time.sleep(.1)\n"
            )
            worker.chmod(0o700)
            test_terminal.BINARY = str(root / "papo-tui")
            with self.helper.serve(media.MediaTerminal.images["png"], "image/png") as server:
                t = self.open(server)
                try:
                    self.wait(t, lambda: bool(self.children(t)))
                    t.send("rascunho sem bloqueio")
                    self.choose(t, "ultralight")
                    self.wait(t, lambda: not self.children(t), timeout=3)
                    self.helper.wait(t, "rascunho sem bloqueio")
                finally:
                    self.assertTrue(t.close())

    def test_ultralight_runs_without_media_helper(self):
        self.seed("ultralight")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory, "papo-tui")
            shutil.copy2(BINARY, executable)
            test_terminal.BINARY = str(executable)
            with self.helper.serve(media.MediaTerminal.images["png"], "image/png") as server:
                t = self.open(server)
                try:
                    self.helper.wait(t, "Imagem: Fixture")
                    self.assertEqual(self.children(t), [])
                    self.assert_no_media_libs(t)
                finally:
                    self.assertTrue(t.close())

    def test_worker_crash_preserves_terminal_and_draft(self):
        self.seed("light")
        with self.helper.serve(media.MediaTerminal.animated_gif, "image/gif") as server:
            t = self.open(server)
            try:
                self.wait(t, lambda: bool(self.children(t)))
                for pid in self.children(t):
                    os.kill(pid, signal.SIGKILL)
                t.send("continua funcionando")
                self.helper.wait(t, "continua funcionando")
                self.assertIsNone(t.process.poll())
            finally:
                self.assertTrue(t.close())


if __name__ == "__main__":
    unittest.main()
