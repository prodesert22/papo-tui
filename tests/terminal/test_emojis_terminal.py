"""Emoji picker, authenticated assets and retained pictures in a real PTY."""

import base64
import fcntl
from datetime import datetime, timezone
import signal
import re
import struct
import subprocess
import termios
import threading
import time
import unittest
from urllib.parse import urlsplit, parse_qs
import test_media_terminal as media_tests
from history_mock import HistoryMock, CHANNEL, STAMP, uuid
from realtime_mock import RealtimeMock
from test_terminal import editor_region, editor_column


def encode_emoji_animation(gif, format):
    encoder, container = {
        "GIF": ("gif", "gif"),
        "WEBP": ("libwebp_anim", "webp"),
        "PNG": ("apng", "apng"),
    }[format]
    if format == "WEBP":
        decoders = subprocess.run(
            [media_tests.FFMPEG, "-hide_banner", "-decoders"], capture_output=True, check=True
        ).stdout.split()
        if b"webp_anim" not in decoders:
            raise unittest.SkipTest("Installed FFmpeg does not provide webp_anim")
    return subprocess.run(
        [
            media_tests.FFMPEG,
            "-v",
            "error",
            "-i",
            "pipe:0",
            "-c:v",
            encoder,
            "-frames:v",
            "2",
            "-threads",
            "1",
            "-f",
            container,
            "pipe:1",
        ],
        input=gif,
        capture_output=True,
        check=True,
    ).stdout


class EmojiTerminal(unittest.TestCase):
    include_icons = True
    wait = media_tests.MediaTerminal.wait
    open = media_tests.MediaTerminal.open
    tearDown = media_tests.MediaTerminal.tearDown

    @classmethod
    def setUpClass(cls):
        media_tests.MediaTerminal.setUpClass()
        cls.png = media_tests.MediaTerminal.images["png"]
        cls.gif = media_tests.MediaTerminal.animated_gif

    @classmethod
    def tearDownClass(cls):
        media_tests.MediaTerminal.tearDownClass()

    def until(self, t, predicate, timeout=8):
        deadline = time.monotonic() + timeout
        while not predicate() and time.monotonic() < deadline:
            t.read(0.03)
        self.assertTrue(predicate(), "\n".join(t.screen.display))

    def server(
        self,
        *,
        content="Mensagem 0001",
        count=1,
        animated=False,
        large=False,
        status=200,
        realtime=False,
    ):
        s = (RealtimeMock if realtime else HistoryMock)(count=count)
        for m in s.messages:
            m.update(content=content, created_at=datetime.now(timezone.utc).isoformat())
        parent = s.route
        blob = base64.b64encode(
            (self.gif if animated else self.png)
            + (b"x" * (600000 if animated else 120000) if large else b"")
        ).decode()
        s.emojis = [
            {
                "id": uuid(900 + i),
                "name": f"gato{i}" if i else "gato 界",
                "format": "GIF" if animated else "PNG",
                "image_blob": blob,
                "created_at": "2026-10-14T10:00:00.000000Z",
            }
            for i in range(26 if large else 1)
        ]
        avatar = base64.b64encode(
            self.gif if animated else self.png + (b"x" * 900000 if large else b"")
        ).decode()

        def route(method, path, body):
            url = urlsplit(path)
            if url.path.endswith("/emojis"):
                if status != 200:
                    return status, None
                cursor = parse_qs(url.query).get("last_id", [""])[0]
                first = 25 if cursor else 0
                return 200, {
                    "emojis": s.emojis[first : first + 25],
                    "has_more": first + 25 < len(s.emojis),
                }
            if url.path.endswith("/users/profile_batch"):
                self.assertLessEqual(len(body["ids"]), 4)
                self.assertEqual(len(body["ids"]), len(set(body["ids"])))
                return 200, {
                    "profiles": [
                        {
                            "id": id,
                            "username": "Autor",
                            "avatar_blob": avatar,
                            "avatar_format": "GIF" if animated else "PNG",
                        }
                        for id in body["ids"]
                    ]
                }
            return parent(method, path, body)

        s.api_handler = route
        return s

    def avatar_positions(self, t):
        images = {
            int(h[b"i"])
            for h, p in t.stream.kitty_frames
            if h.get(b"s") == b"48" and h.get(b"v") == b"48"
        }
        # Enlarged body emojis are also 48 pixels; avatars occupy the gutter.
        return {
            (int(placement[b"i"]), int(placement[b"p"]))
            for placement, position in zip(t.stream.placements, t.stream.placement_positions)
            if int(placement[b"i"]) in images and position[1] in (3, 28)
        } & t.stream.live_placements

    def test_mention_and_notification_panels_retain_avatars_and_inline_emoji(self):
        for key, title in (("@", "Menções"), ("\x1b[20~", "Notificações")):
            with self.subTest(panel=title), self.server(content="Emoji 😀") as server:
                t = self.open(server, protocol="kitty", size=(48, 160))
                try:
                    t.send("draft ")
                    self.until(t, lambda: bool(self.avatar_positions(t)))
                    self.until(
                        t, lambda: any(h.get(b"s") == b"16" for h, _ in t.stream.kitty_frames)
                    )
                    images = {
                        int(p[b"i"])
                        for p, (y, _) in zip(t.stream.placements, t.stream.placement_positions)
                        if y < 20 and int(p[b"i"]) in t.stream.live_ids
                    }
                    self.assertGreaterEqual(len(images), 2)
                    t.send(key)
                    self.wait(t, title)
                    self.until(t, lambda: images <= t.stream.live_ids)
                    t.send("\x12")
                    self.wait(t, title)
                    self.until(t, lambda: images <= t.stream.live_ids)
                    t.send("\x1b")
                    self.until(t, lambda: title not in "\n".join(t.screen.display))
                    self.until(t, lambda: images <= t.stream.live_ids)
                finally:
                    self.assertTrue(t.close())

    def test_keyboard_unicode_inserts_at_cursor_without_sending(self):
        with self.server() as s:
            t = self.open(s, protocol="kitty")
            try:
                t.send("antes depois\x1b[1;5D\x1bOS")
                self.wait(t, "Emojis · Esc voltar")
                t.send("family: man, woman, girl, boy")
                self.wait(t, "família")
                t.send("\n")
                self.until(t, lambda: "Emojis · Esc voltar" not in "\n".join(t.screen.display))
                self.assertNotIn("Emojis · Esc voltar", "\n".join(t.screen.display))
                self.assertEqual(s.message_count, 0)
                t.send("\n")
                self.until(t, lambda: s.message_count == 1)
                self.assertEqual(s.sent_messages[0]["content"], "antes 👨‍👩‍👧‍👦depois")
            finally:
                self.assertTrue(t.close())

    def test_colon_completion_filters_custom_and_unicode_without_sending(self):
        with self.server() as s:
            t = self.open(s, protocol="kitty")
            try:
                t.send(":")
                self.wait(t, "Emojis · mais usados")
                t.send("gato_界")
                self.wait(t, "gato 界")
                t.send("\t")
                self.until(t, lambda: "Emojis · mais usados" not in "\n".join(t.screen.display))
                self.assertEqual(s.message_count, 0)
                t.send(" :rocket")
                self.wait(t, "Emojis · mais usados")
                t.send("\n")
                self.until(t, lambda: "Emojis · mais usados" not in "\n".join(t.screen.display))
                self.assertEqual(s.message_count, 0)
                t.send("\n")
                self.until(t, lambda: s.message_count == 1)
                self.assertEqual(s.sent_messages[0]["content"], ":gato 界: 🚀")
            finally:
                self.assertTrue(t.close())

    def test_picker_arrows_visit_search_tabs_and_first_grid_row(self):
        with self.server() as s:
            t = self.open(s, sixel=False)
            try:
                t.send("\x1bOS\x1bOB\x1bOC")
                self.wait(t, "1 emojis do servidor")
                # Search -> tabs -> first tile -> tabs -> search.
                t.send("\x1bOB\x1bOA\x1bOD\x1bOArocket\n")
                self.until(t, lambda: "Emojis · Esc voltar" not in "\n".join(t.screen.display))
                self.assertEqual(s.message_count, 0)
                t.send("\n")
                self.until(t, lambda: s.message_count == 1)
                self.assertEqual(s.sent_messages[0]["content"], "🚀")
            finally:
                self.assertTrue(t.close())

    def test_modal_cancel_and_paste_do_not_reopen_completion(self):
        with self.server() as s:
            t = self.open(s, sixel=False)
            try:
                t.send(":rocket")
                self.wait(t, "Emojis · mais usados")
                t.send("\x1bOS")
                self.wait(t, "Emojis · Esc voltar")
                t.send("\x1b")
                t.read(0.3)
                self.assertNotIn("Emojis ·", "\n".join(t.screen.display))
                t.send("\x03\x1b[200~:rocket\x1b[201~")
                t.read(0.3)
                self.assertNotIn("Emojis ·", "\n".join(t.screen.display))
                self.assertEqual(s.message_count, 0)
                t.send("\n")
                self.until(t, lambda: s.message_count == 1)
                self.assertEqual(s.sent_messages[0]["content"], ":rocket")
            finally:
                self.assertTrue(t.close())

    def test_picker_has_32_pixel_previews_labels_and_retains_uncovered_images(self):
        for protocol in ("kitty", "sixel", "iterm2"):
            with self.subTest(protocol=protocol), self.server(content="Emoji 😀 :gato 界:") as s:
                t = self.open(s, protocol=protocol, size=(48, 100))
                try:
                    self.until(t, lambda: bool(t.stream.frames))
                    avatars = self.avatar_positions(t) if protocol == "kitty" else set()
                    t.send("\x1bOS\t\x1bOC")
                    self.wait(t, "1 emojis do servidor")
                    if protocol == "kitty":
                        self.until(
                            t,
                            lambda: any(
                                h.get(b"s") == b"32" and h.get(b"v") == b"32"
                                for h, _ in t.stream.kitty_frames
                            ),
                        )
                        self.assertTrue(avatars <= self.avatar_positions(t))
                        top = next(
                            i
                            for i, row in enumerate(t.screen.display)
                            if "Emojis · Esc voltar" in row
                        )
                        self.assertIn("gato 界", t.screen.display[top + 6])
                        before = len(t.stream.kitty_frames)
                        t.send("\t\x1bOC\x1bOD")
                        t.read(0.3)
                        self.assertEqual(len(t.stream.kitty_frames), before)
                    t.send("\x1b")
                    self.until(t, lambda: "Emojis · Esc voltar" not in "\n".join(t.screen.display))
                    if protocol == "kitty":
                        self.assertTrue(avatars <= self.avatar_positions(t))
                finally:
                    self.assertTrue(t.close())

    def test_button_focus_custom_emoji_and_outside_click(self):
        with self.server() as s:
            t = self.open(s, sixel=False)
            try:
                t.send("rascunho\t")
                self.wait(t, "Foco: Emojis")
                t.send("\n")
                self.wait(t, "Emojis · Esc voltar")
                t.send("\t\x1bOC")
                self.wait(t, "1 emojis do servidor")
                t.send("\t\n")
                self.assertEqual(s.message_count, 0)
                first, _ = editor_region(t)
                # Chafa fallback renders the custom emoji as cells. The send
                # assertion below verifies its exact underlying text.
                self.assertIn("rascunho", t.screen.display[first])
                t.click(first, t.screen.columns - 7)
                self.wait(t, "Emojis · Esc voltar")
                reads = sum("/channels/" in r[1] and "/messages" in r[1] for r in s.requests)
                t.click(8, 4)
                t.read(0.2)
                self.assertNotIn("Emojis · Esc voltar", "\n".join(t.screen.display))
                self.assertEqual(
                    sum("/channels/" in r[1] and "/messages" in r[1] for r in s.requests), reads
                )
                t.send("\n")
                self.until(t, lambda: s.message_count == 1)
                self.assertEqual(s.sent_messages[0]["content"], "rascunho:gato 界:")
            finally:
                self.assertTrue(t.close())

    def test_participants_render_custom_emoji_above_the_modal_background(self):
        self.check_participant_images(custom=True)

    def test_participants_retire_unicode_images_shared_with_history(self):
        self.check_participant_images(custom=False)

    def check_participant_images(self, *, custom):
        content = "Mensagem 0001 :gato 界:" if custom else "Mensagem 0001 😀"
        for protocol in ("kitty", "sixel", "iterm2", "chafa") if custom else ("kitty",):
            with self.subTest(protocol=protocol), self.server(content=content) as s:
                emoji = dict(
                    emoji_id=s.emojis[0]["id"] if custom else None,
                    unicode=None if custom else "😀",
                    count=2,
                )
                s.messages[0]["reactions"] = [emoji]
                parent = s.api_handler

                def route(method, path, body):
                    if method == "GET" and urlsplit(path).path.endswith("/reactions"):
                        return 200, {
                            "message_id": s.messages[0]["id"],
                            "reactions": [
                                dict(
                                    emoji,
                                    users=[
                                        dict(
                                            id=uuid(1200 + i),
                                            user_id=uuid(30 + i),
                                            created_at=STAMP,
                                        )
                                        for i in range(2)
                                    ],
                                )
                            ],
                            "has_more": False,
                        }
                    return parent(method, path, body)

                s.api_handler = route
                t = self.open(s, protocol=protocol, sixel=protocol != "chafa", size=(32, 100))
                try:
                    self.wait(t, "Mensagem 0001")
                    t.send("\x1b[18~")
                    t.read(0.5)
                    t.send("E")
                    self.wait(t, "Quem reagiu")
                    self.wait(t, "Fim da lista")
                    header = next(
                        i
                        for i, line in enumerate(t.screen.display)
                        if "Reações" in line and "Pessoas" in line
                    )
                    position = (header + 2, t.screen.display[header].index("Reações") + 1)

                    def placements():
                        return {
                            (int(p[b"i"]), int(p[b"p"]))
                            for p, point in zip(t.stream.placements, t.stream.placement_positions)
                            if point == position
                        } & t.stream.live_placements

                    if protocol == "kitty":
                        self.until(t, lambda: bool(placements()))
                        t.read(0.5)
                        panel_icons = placements()
                        # A fallback and a newly encoded selection background
                        # can use the same occurrence with different image IDs.
                        # Include every image ever placed in the modal, so an
                        # older shared image cannot escape the close assertion.
                        panel_icons |= {
                            (int(p[b"i"]), int(p[b"p"]))
                            for p, point in zip(t.stream.placements, t.stream.placement_positions)
                            if point == position
                        }
                    elif protocol != "chafa":
                        self.until(t, lambda: position in t.stream.frame_positions)
                    else:
                        self.until(t, lambda: ":emoji:" not in t.screen.display[position[0]])
                    row = t.screen.display[position[0]]
                    self.assertNotIn(":gato 界:", row)
                    self.assertNotIn(":emoji:", row)
                    self.assertIn(" 2", row)
                    t.send("\x1b")
                    self.until(t, lambda: "Quem reagiu" not in "\n".join(t.screen.display))
                    if protocol == "kitty":
                        self.assertFalse(panel_icons & t.stream.live_placements)
                    self.assertTrue(
                        all(
                            t.screen.buffer[position[0]][column].data == " "
                            for column in range(position[1], position[1] + 2)
                        ),
                        repr(t.screen.display[position[0]]),
                    )
                    self.assertEqual(s.message_count, 0)
                finally:
                    self.assertTrue(t.close())

    def test_paging_large_base64_responses_and_no_repeat_reads(self):
        with self.server(content=":gato25:", large=True) as s:
            t = self.open(s, protocol="kitty")
            try:
                self.until(t, lambda: sum("/emojis?" in r[1] for r in s.requests) == 2)
                self.until(t, lambda: bool(t.stream.frames))
                t.send("\x1bOS\t\x1bOC")
                self.wait(t, "26 emojis do servidor")
                t.send("\x1b")
                self.until(t, lambda: any("/users/profile_batch" in r[1] for r in s.requests))
                t.read(0.5)

                def relevant():
                    return sum(
                        "/emojis?" in r[1] or "/users/profile_batch" in r[1] for r in s.requests
                    )

                count = relevant()
                t.send("a")
                t.read(0.3)
                self.assertEqual(relevant(), count)
                self.assertTrue(any("/users/profile_batch" in r[1] for r in s.requests))
                self.assertTrue(
                    all(
                        r[2] == "Auth=" + s.session_cookie
                        for r in s.requests
                        if "/emojis?" in r[1] or "/users/profile_batch" in r[1]
                    )
                )
            finally:
                self.assertTrue(t.close())

    def test_repeated_unicode_shares_uploads_and_does_not_flash(self):
        with self.server(content="😄 " * 90) as s:
            # Keep enough enlarged emojis visible to exercise shared uploads.
            t = self.open(s, protocol="kitty", size=(48, 100))
            try:
                # Focus the composer before measuring repeated input redraws;
                # changing focus can create a new avatar background variant.
                t.send("x")
                # The button uses the same icon but a different background.
                self.until(t, lambda: len(t.stream.placements) > 65)
                by_image = {}
                for placement in t.stream.placements:
                    by_image.setdefault(placement[b"i"], set()).add(placement[b"p"])
                self.assertGreater(max(map(len, by_image.values())), 64)
                shared = max(by_image, key=lambda id: len(by_image[id]))
                pixels = next(p for h, p in t.stream.kitty_frames if h[b"i"] == shared)
                self.assertTrue(
                    any(
                        r > g + 15 and g > b + 30
                        for r, g, b in zip(pixels[::4], pixels[1::4], pixels[2::4])
                    ),
                    "Unicode icon must contain color pixels",
                )
                self.until(t, lambda: bool(self.avatar_positions(t)))
                t.read(0.3)
                uploads = len(t.stream.frames)
                t.send("a")
                t.send("b")
                t.read(0.2)
                self.assertEqual(len(t.stream.frames), uploads)
            finally:
                self.assertTrue(t.close())

    def test_custom_and_avatar_gifs_animate_only_while_visible(self):
        with self.server(content="Emoji :gato 界:", animated=True) as s:
            t = self.open(s, protocol="kitty")
            try:
                self.until(t, lambda: len(t.stream.frames) > 5)
                ids = {header[b"i"] for header, _ in t.stream.kitty_frames}
                self.assertGreaterEqual(len(ids), 3)
                requests = len(s.requests)
                t.read(0.3)
                self.assertEqual(len(s.requests), requests)
                variants = {}
                for header, pixels in t.stream.kitty_frames:
                    if header.get(b"s") == b"16" and header.get(b"v") == b"16":
                        variants.setdefault(header[b"i"], set()).add(pixels)
                animated_ids = {id for id, pixels in variants.items() if len(pixels) > 1}
                self.assertTrue(animated_ids)
                t.click(8, 4)
                self.wait(t, "Conteúdo do outro canal")
                before = len(t.stream.kitty_frames)
                t.read(0.6)
                self.assertFalse(
                    any(
                        header[b"i"] in animated_ids for header, _ in t.stream.kitty_frames[before:]
                    )
                )
            finally:
                self.assertTrue(t.close())

    def test_custom_gifs_animate_in_messages_and_picker_across_graphics_protocols(self):
        for protocol in ("kitty", "sixel", "iterm2"):
            with (
                self.subTest(protocol=protocol),
                self.server(content="Emoji :gato 界: fim", animated=True) as server,
            ):
                t = self.open(server, protocol=protocol, size=(48, 100))
                try:
                    self.until(t, lambda: sum("/emojis?" in r[1] for r in server.requests) == 1)

                    def pixels(size):
                        if protocol == "kitty":
                            return {
                                data
                                for header, data in t.stream.kitty_frames
                                if media_tests.raster_dimensions(protocol, header) == size
                            }
                        return {
                            frame
                            if protocol == "sixel"
                            else base64.b64decode(frame.split(b":", 1)[1][:-1])
                            for frame in t.stream.frames
                            if media_tests.raster_dimensions(protocol, frame) == size
                        }

                    self.until(t, lambda: len(pixels((16, 16))) >= 2)
                    t.send("\x1bOS\t\x1bOC")
                    self.wait(t, "1 emojis do servidor")
                    self.until(t, lambda: len(pixels((32, 32))) >= 2)
                    t.resize(44, 110)
                    self.wait(t, "1 emojis do servidor")
                    t.send("\x1b")
                    self.until(t, lambda: "Emojis · Esc voltar" not in "\n".join(t.screen.display))
                    before = len(t.stream.frames)
                    self.until(t, lambda: len(t.stream.frames) > before + 2)
                    self.assertEqual(sum("/emojis?" in r[1] for r in server.requests), 1)
                    self.assertEqual(server.message_count, 0)
                finally:
                    self.assertTrue(t.close())

    def image_positions(self, t, protocol, size, start=0):
        if protocol == "kitty":
            images = {
                int(h[b"i"])
                for h, _ in t.stream.kitty_frames
                if media_tests.raster_dimensions(protocol, h) == size
            }
            return {
                position
                for placement, position in zip(
                    t.stream.placements[start:], t.stream.placement_positions[start:]
                )
                if int(placement[b"i"]) in images and position[1] >= 34
            }
        return {
            position
            for frame, position in zip(t.stream.frames[start:], t.stream.frame_positions[start:])
            if media_tests.raster_dimensions(protocol, frame) == size and position[1] >= 34
        }

    def test_emoji_only_messages_use_large_icons_and_keep_text_and_reactions_inline(self):
        for protocol in ("kitty", "sixel", "iterm2"):
            with self.subTest(protocol=protocol), self.server(count=5, animated=True) as server:
                for message in server.messages:
                    message["author_id"] = uuid(21)
                server.messages[0]["content"] = "😀 :gato 界: 👨‍👩‍👧‍👦"
                server.messages[0]["reactions"] = [
                    dict(emoji_id=server.emojis[0]["id"], unicode=None, count=2)
                ]
                server.messages[1]["content"] = "Texto 😀 :gato 界:"
                server.messages[2]["content"] = "`😀` :not_loaded:"
                server.messages[3]["content"] = "123 #"
                server.messages[4]["content"] = "Depois"
                t = self.open(server, protocol=protocol, size=(48, 160))
                try:
                    self.wait(t, "Depois")
                    self.wait(t, ":not_loaded:")
                    self.wait(t, "123 #")
                    self.until(t, lambda: len(self.image_positions(t, protocol, (48, 48))) >= 3)
                    reaction_row = next(
                        i for i, line in enumerate(t.screen.display) if " 2]" in line
                    )
                    text_row = next(
                        i for i, line in enumerate(t.screen.display) if "Texto " in line
                    )
                    expected = {(reaction_row - 3, x) for x in (34, 41, 48)}
                    self.assertTrue(expected <= self.image_positions(t, protocol, (48, 48)))
                    self.until(
                        t, lambda: (reaction_row, 35) in self.image_positions(t, protocol, (16, 16))
                    )
                    self.until(
                        t, lambda: (text_row, 40) in self.image_positions(t, protocol, (16, 16))
                    )
                    if protocol == "kitty":
                        images = {
                            int(p[b"i"])
                            for p, pos in zip(t.stream.placements, t.stream.placement_positions)
                            if pos in expected
                        }
                        self.until(t, lambda: images <= t.stream.live_ids)
                    t.send("\x1bOS\t\x1bOC")
                    self.wait(t, "1 emojis do servidor")
                    if protocol == "kitty":
                        self.until(t, lambda: images <= t.stream.live_ids)
                    t.send("\x1b")
                    self.until(t, lambda: "Emojis · Esc voltar" not in "\n".join(t.screen.display))
                    self.wait(t, "Depois")
                    self.assertEqual(server.message_count, 0)
                finally:
                    self.assertTrue(t.close())

    def test_large_emojis_wrap_on_resize_and_crop_at_history_edges(self):
        for protocol in ("kitty", "sixel", "iterm2"):
            with self.subTest(protocol=protocol), self.server(count=2, animated=True) as server:
                for message in server.messages:
                    message["author_id"] = uuid(21)
                server.messages[0]["content"] = ":gato 界:" * 18 + "\n" + ":gato 界:" * 2
                server.messages[1]["content"] = "Depois"
                t = self.open(server, protocol=protocol, size=(48, 100))
                try:
                    self.wait(t, "Depois")
                    self.until(t, lambda: len(self.image_positions(t, protocol, (48, 48))) >= 20)
                    before_row = next(
                        i for i, line in enumerate(t.screen.display) if "Depois" in line
                    )
                    t.resize(48, 80)
                    self.wait(t, "Depois")
                    after_row = next(
                        i for i, line in enumerate(t.screen.display) if "Depois" in line
                    )
                    self.assertGreater(after_row, before_row)
                    t.resize(22, 80)
                    t.send("\x1bOH")
                    t.read(0.3)
                    start = len(t.stream.placements if protocol == "kitty" else t.stream.frames)
                    t.send("\x1bOB\x1bOB")
                    if protocol == "kitty":
                        self.until(
                            t,
                            lambda: any(
                                p.get(b"y") == b"16"
                                and p.get(b"h") == b"32"
                                and pos[0] == 8
                                and pos[1] >= 34
                                for p, pos in zip(
                                    t.stream.placements[start:],
                                    t.stream.placement_positions[start:],
                                )
                            ),
                        )
                    else:
                        self.until(
                            t,
                            lambda: any(
                                y == 8
                                for y, _ in self.image_positions(t, protocol, (48, 32), start)
                            ),
                        )
                    self.assertEqual(server.message_count, 0)
                finally:
                    self.assertTrue(t.close())

    def test_animated_emoji_larger_than_old_blob_and_page_limits(self):
        self.gif = subprocess.run(
            [
                media_tests.FFMPEG,
                "-v",
                "error",
                "-i",
                "pipe:0",
                "-vf",
                "scale=640:640:flags=neighbor",
                "-frames:v",
                "2",
                "-threads",
                "1",
                "-f",
                "gif",
                "pipe:1",
            ],
            input=self.gif,
            capture_output=True,
            check=True,
        ).stdout
        with self.server(content="Grande :gato25: fim", animated=True, large=True) as server:
            t = self.open(server, protocol="kitty")
            try:
                self.until(
                    t, lambda: sum("/emojis?" in r[1] for r in server.requests) == 2, timeout=30
                )
                self.until(t, lambda: ":gato25:" not in "\n".join(t.screen.display), timeout=30)

                def pixels():
                    row = next(i for i, line in enumerate(t.screen.display) if "Grande " in line)
                    column = t.screen.display[row].index("Grande ") + len("Grande ")
                    images = {
                        int(placement[b"i"])
                        for placement, position in zip(
                            t.stream.placements, t.stream.placement_positions
                        )
                        if position == (row, column)
                    }
                    return {
                        data
                        for header, data in t.stream.kitty_frames
                        if int(header[b"i"]) in images
                    }

                self.until(t, lambda: len(pixels()) >= 2)
                self.assertEqual(sum("/emojis?" in r[1] for r in server.requests), 2)
                self.assertNotIn(":gato25:", "\n".join(t.screen.display))
            finally:
                self.assertTrue(t.close())

    def test_animated_server_formats_render_at_message_and_reaction_positions(self):
        for declared in ("GIF", "WEBP", "PNG"):
            with self.subTest(format=declared):
                self.gif = encode_emoji_animation(type(self).gif, declared)
                with self.server(content="Texto :gato 界: fim", animated=True) as server:
                    server.emojis[0]["format"] = declared
                    server.messages[0]["reactions"] = [
                        dict(emoji_id=server.emojis[0]["id"], unicode=None, count=2)
                    ]
                    t = self.open(server, protocol="kitty", size=(40, 100))
                    try:
                        self.wait(t, "Texto ")
                        self.until(t, lambda: any(" 2]" in line for line in t.screen.display))

                        def actual_pixels(reaction):
                            marker = " 2]" if reaction else "Texto "
                            row = next(
                                i for i, line in enumerate(t.screen.display) if marker in line
                            )
                            column = (
                                t.screen.display[row].index("[") + 1
                                if reaction
                                else t.screen.display[row].index(marker) + len(marker)
                            )
                            images = {
                                int(placement[b"i"])
                                for placement, position in zip(
                                    t.stream.placements, t.stream.placement_positions
                                )
                                if position == (row, column)
                            }
                            return {
                                pixels
                                for header, pixels in t.stream.kitty_frames
                                if int(header[b"i"]) in images
                            }

                        self.until(t, lambda: len(actual_pixels(False)) > 1)
                        self.until(t, lambda: len(actual_pixels(True)) > 1)
                        self.assertNotIn(":gato 界:", "\n".join(t.screen.display))
                        t.send("\x1bOS\t\x1bOC")
                        self.wait(t, "1 emojis do servidor")
                        self.until(t, lambda: ":emoji:" not in "\n".join(t.screen.display))
                        self.until(
                            t,
                            lambda: any(
                                len(
                                    {
                                        pixels
                                        for header, pixels in t.stream.kitty_frames
                                        if header[b"i"] == image
                                    }
                                )
                                > 1
                                for image in {
                                    header[b"i"]
                                    for header, _ in t.stream.kitty_frames
                                    if header.get(b"s") == b"32" and header.get(b"v") == b"32"
                                }
                            ),
                        )
                        t.send("\t\n")
                        self.until(
                            t, lambda: "Emojis · Esc voltar" not in "\n".join(t.screen.display)
                        )

                        def editor_pixels():
                            row, _ = editor_region(t)
                            column = editor_column(t)
                            images = {
                                int(placement[b"i"])
                                for placement, position in zip(
                                    t.stream.placements, t.stream.placement_positions
                                )
                                if position == (row, column)
                            }
                            return {
                                pixels
                                for header, pixels in t.stream.kitty_frames
                                if int(header[b"i"]) in images
                            }

                        self.until(t, lambda: len(editor_pixels()) > 1)
                        self.assertEqual(server.message_count, 0)
                        self.assertEqual(sum("/emojis?" in r[1] for r in server.requests), 1)
                    finally:
                        self.assertTrue(t.close())

    def test_search_resize_cancel_limit_and_error(self):
        with self.server(status=429) as s:
            t = self.open(s, sixel=False)
            try:
                t.send("\x1b[200~" + "x" * 2048 + "\x1b[201~\x1bOS")
                self.wait(t, "Emojis · Esc voltar")
                t.send("grinning face with smiling eyes\n")
                self.wait(t, "excede o limite")
                t.resize(14, 44)
                self.wait(t, "Emojis · Esc voltar")
                t.send("\x1b")
                self.assertEqual(s.message_count, 0)
                t.send("\x1bOS\x03\t\x1bOC")
                self.wait(t, "Limite atingido")
                t.resize(30, 100)
                self.wait(t, "Emojis · Esc voltar")
                t.send("\x1b")
                self.assertEqual(s.message_count, 0)
            finally:
                self.assertTrue(t.close())

    def test_profile_401_recovers_once_without_losing_read_only_post(self):
        with self.server() as s:
            route = s.api_handler
            reads = []

            def auth_once(method, path, body):
                if path.endswith("/users/profile_batch"):
                    reads.append(body)
                    if len(reads) == 1:
                        return 401, None
                return route(method, path, body)

            s.api_handler = auth_once
            t = self.open(s, protocol="kitty")
            try:
                self.until(
                    t,
                    lambda: any(
                        h.get(b"s") == b"48" and h.get(b"v") == b"48"
                        for h, _ in t.stream.kitty_frames
                    ),
                )
                self.assertEqual(len(reads), 2)
                self.assertEqual(reads[0], reads[1])
                self.assertEqual(s.refresh_count, 1)
                self.assertEqual(s.message_count, 0)
            finally:
                self.assertTrue(t.close())

    def test_member_avatars_do_not_wait_for_names_or_emoji_pages(self):
        for blocked in ("/users/user_summary_batch", "/emojis?"):
            with (
                self.subTest(blocked=blocked),
                self.server(content=":gato25:", large=True, count=6) as s,
            ):
                for i, m in enumerate(s.messages):
                    m["author_id"] = uuid(40 + i)
                route = s.api_handler
                release = threading.Event()
                entered = threading.Event()

                def delayed(method, path, body):
                    if blocked in path:
                        entered.set()
                        release.wait(8)
                    return route(method, path, body)

                s.api_handler = delayed
                t = self.open(s, protocol="kitty", size=(48, 100))
                try:
                    self.until(t, lambda: len(self.avatar_positions(t)) == 6, timeout=3)
                    self.assertFalse(release.is_set())
                    self.until(t, entered.is_set)
                    calls = [r[1] for r in s.requests]
                    self.assertLess(
                        next(i for i, p in enumerate(calls) if "/users/profile_batch" in p),
                        next(i for i, p in enumerate(calls) if blocked in p),
                    )
                finally:
                    release.set()
                    self.assertTrue(t.close())

    def test_large_member_avatars_survive_redraw_and_channel_return(self):
        with self.server(count=6) as s:
            members = {uuid(40 + i) for i in range(6)}
            for i, m in enumerate(s.messages):
                m["author_id"] = uuid(40 + i)
            route = s.api_handler
            reads = []
            blob = base64.b64encode(self.png + b"x" * 1500000).decode()

            def profiles(method, path, body):
                if path.endswith("/users/profile_batch"):
                    reads.extend(body["ids"])
                    self.assertLessEqual(len(body["ids"]), 4)
                    return 200, {
                        "profiles": [
                            {"id": id, "avatar_blob": blob, "avatar_format": "PNG"}
                            for id in body["ids"]
                        ]
                    }
                return route(method, path, body)

            s.api_handler = profiles
            t = self.open(s, protocol="kitty", size=(48, 100))
            try:
                self.until(t, lambda: len(self.avatar_positions(t)) == 6)
                t.send("x")
                t.read(0.2)
                self.assertEqual(sorted(id for id in reads if id in members), sorted(members))

                def other_reads():
                    return sum("/channels/" + uuid(11) + "/messages" in r[1] for r in s.requests)

                self.assertEqual(other_reads(), 0)
                t.click(8, 4)
                self.wait(t, "Conteúdo do outro canal")
                self.until(t, lambda: len(self.avatar_positions(t)) == 1)
                t.click(7, 4)
                self.wait(t, "Mensagem 0001")
                self.until(t, lambda: len(self.avatar_positions(t)) == 6)
                t.read(0.2)
                self.assertEqual(sorted(id for id in reads if id in members), sorted(members))
                self.assertEqual(other_reads(), 1)
            finally:
                self.assertTrue(t.close())

    def test_profile_errors_preserve_initials_and_wait_for_manual_retry(self):
        for status in (403, 404, 429, 500):
            with self.subTest(status=status), self.server() as s:
                route = s.api_handler

                def fail_profile(method, path, body):
                    if path.endswith("/users/profile_batch"):
                        return status, None
                    return route(method, path, body)

                s.api_handler = fail_profile
                t = self.open(s, protocol="kitty")
                try:

                    def reads():
                        return sum("/users/profile_batch" in r[1] for r in s.requests)

                    self.until(t, lambda: reads() == 1)
                    t.read(0.4)
                    self.assertEqual(reads(), 1)
                    self.wait(t, "Apelido")
                    self.assertNotIn("URL do servidor", "\n".join(t.screen.display))
                finally:
                    self.assertTrue(t.close())

    def test_scrolling_past_128_authors_reuses_their_avatars(self):
        with self.server(count=160) as s:
            for n, message in enumerate(s.messages, 1):
                message.update(
                    author_id=uuid(5000 + n), content=f"Mensagem {n:04d}", created_at=STAMP
                )
            route = s.api_handler
            reads = []

            def profiles(method, path, body):
                if path.endswith("/users/profile_batch"):
                    reads.extend(body["ids"])
                return route(method, path, body)

            s.api_handler = profiles
            t = self.open(s, protocol="kitty", size=(100, 100))
            try:
                self.wait(t, "Mensagem 0160")

                def visible():
                    return {
                        uuid(5000 + int(n))
                        for n in re.findall(r"Mensagem (\d{4})", "\n".join(t.screen.display))
                    }

                recent = visible()
                for _ in range(12):
                    users = visible()
                    self.until(t, lambda: users.issubset(set(reads)))
                    self.until(t, lambda: len(self.avatar_positions(t)) >= max(1, len(users) - 1))
                    if "Mensagem 0001" in "\n".join(t.screen.display):
                        break
                    t.send("\x1b[5~")
                self.assertGreater(len(set(reads)), 128)
                t.send("\x1bOF")
                self.wait(t, "Mensagem 0160")
                self.until(t, lambda: len(self.avatar_positions(t)) >= max(1, len(recent) - 1))
                t.read(0.2)
                self.assertTrue(all(reads.count(user) == 1 for user in recent))
                self.assertEqual(len(reads), len(set(reads)))
            finally:
                self.assertTrue(t.close())

    def test_avatar_update_keeps_cached_image_while_loading_replacement(self):
        with self.server(realtime=True) as s:
            route = s.api_handler
            started, release = threading.Event(), threading.Event()
            reads = []

            def profiles(method, path, body):
                if path.endswith("/users/profile_batch"):
                    reads.append(body["ids"])
                    if len(reads) > 1:
                        started.set()
                        release.wait(8)
                        return 200, {
                            "profiles": [
                                {
                                    "id": user,
                                    "avatar_blob": base64.b64encode(self.gif).decode(),
                                    "avatar_format": "GIF",
                                }
                                for user in body["ids"]
                            ]
                        }
                return route(method, path, body)

            s.api_handler = profiles
            t = self.open(s, protocol="kitty")
            try:
                self.until(t, lambda: bool(self.avatar_positions(t)) and bool(s.peers))
                previous = self.avatar_positions(t)
                s.emit({"type": "avatar_update", "user_id": s.messages[0]["author_id"]})
                self.until(t, started.is_set)
                t.read(0.2)
                self.assertEqual(self.avatar_positions(t), previous)
                release.set()
                self.until(
                    t,
                    lambda: (
                        bool(self.avatar_positions(t))
                        and self.avatar_positions(t).isdisjoint(previous)
                    ),
                )
                t.send("\x1bOH\x1bOF")
                t.read(0.3)
                self.assertEqual(len(reads), 2)
            finally:
                release.set()
                self.assertTrue(t.close())

    def test_deleted_channel_closes_picker_and_cannot_send(self):
        with self.server(realtime=True) as s:
            t = self.open(s, protocol="kitty")
            try:
                self.until(t, lambda: bool(s.peers))
                t.send("rascunho preservado\x1bOS")
                self.wait(t, "Emojis · Esc voltar")
                s.emit({"type": "channel_delete", "channel_id": CHANNEL})
                self.wait(t, "Canal removido")
                self.assertNotIn("Emojis · Esc voltar", "\n".join(t.screen.display))
                t.send("\n")
                t.read(0.2)
                self.assertEqual(s.message_count, 0)
            finally:
                self.assertTrue(t.close())

    def test_header_avatar_position_local_time_and_edited_marker(self):
        with self.server(content="Corpo alinhado ao autor") as s:
            s.messages[0]["edited_at"] = s.messages[0]["created_at"]
            t = self.open(s, protocol="kitty", environment={"TZ": "UTC", "LC_TIME": "C"})
            try:
                self.wait(t, "editado")
                self.until(
                    t,
                    lambda: any(
                        h.get(b"s") == b"48" and h.get(b"v") == b"48"
                        for h, _ in t.stream.kitty_frames
                    ),
                )
                avatar, pixels = next(
                    (h, p)
                    for h, p in t.stream.kitty_frames
                    if h.get(b"s") == b"48" and h.get(b"v") == b"48"
                )
                # The rectangular fixture fills the 48x48 avatar by cropping,
                # without adding a colored frame around its pixels.
                red = [
                    (i % 48, i // 48)
                    for i in range(48 * 48)
                    if pixels[i * 4] > 230 and pixels[i * 4 + 1] < 25 and pixels[i * 4 + 2] < 25
                ]
                self.assertEqual((min(x for x, y in red), max(x for x, y in red)), (0, 47))
                self.assertEqual((min(y for x, y in red), max(y for x, y in red)), (0, 47))
                self.assertEqual(len(red), 48 * 48)
                rows = t.screen.display
                body = next(i for i, row in enumerate(rows) if "Corpo alinhado ao autor" in row)
                head = next(i for i, row in enumerate(rows) if "editado" in row)
                placement = next(
                    pos
                    for p, pos in zip(t.stream.placements, t.stream.placement_positions)
                    if p[b"i"] == avatar[b"i"]
                )
                self.assertEqual(placement, (head, 28))
                self.assertEqual(body - head, 1)
                self.assertEqual(rows[body].index("Corpo"), rows[head].index("Apelido"))
                self.assertEqual(rows[head].index("Apelido"), 34)
                self.assertNotIn("Editada em:", "\n".join(rows))
                self.assertNotIn("2026-", "\n".join(rows))
                self.assertEqual(sum("/users/profile_batch" in r[1] for r in s.requests), 1)
            finally:
                self.assertTrue(t.close())

    def test_avatar_remains_visible_when_header_scrolls_out(self):
        with self.server() as s:
            s.messages[0]["content"] = "\n".join(f"Linha {n:02d}" for n in range(30))
            t = self.open(s, protocol="kitty")
            try:
                self.wait(t, "Linha 29")
                t.send("\x1bOH")
                self.wait(t, "Apelido")
                self.until(
                    t,
                    lambda: any(
                        h.get(b"s") == b"48" and h.get(b"v") == b"48"
                        for h, p in t.stream.kitty_frames
                    ),
                )
                avatar_ids = {
                    h[b"i"]
                    for h, p in t.stream.kitty_frames
                    if h.get(b"s") == b"48" and h.get(b"v") == b"48"
                }
                before = len(t.stream.placements)
                t.send("\x1bOB")
                self.until(
                    t,
                    lambda: any(
                        p[b"i"] in avatar_ids and p.get(b"y") == b"16" and p.get(b"h") == b"32"
                        for p in t.stream.placements[before:]
                    ),
                )
                self.assertNotIn("Apelido", "\n".join(t.screen.display))
                self.assertIn("Linha 00", t.screen.display[8])
            finally:
                self.assertTrue(t.close())

    def test_message_alignment_tracks_terminal_cell_width(self):
        with self.server(content="Texto alinhado") as s:
            t = self.open(s, protocol="kitty")
            try:
                self.wait(t, "Texto alinhado")
                self.wait(t, "Apelido")
                for cell_width, expected in ((10, 33), (8, 34)):
                    fcntl.ioctl(
                        t.slave,
                        termios.TIOCSWINSZ,
                        struct.pack("HHHH", 24, 80, 80 * cell_width, 24 * 16),
                    )
                    t.process.send_signal(signal.SIGWINCH)

                    def aligned():
                        rows = t.screen.display
                        return any(
                            "Apelido" in row and row.index("Apelido") == expected for row in rows
                        ) and any(
                            "Texto alinhado" in row and row.index("Texto alinhado") == expected
                            for row in rows
                        )

                    self.until(t, aligned)
            finally:
                self.assertTrue(t.close())

    def test_avatar_fills_48_pixels_without_padding_across_focus_changes(self):
        environments = (
            {"TERM": "xterm-kitty", "KITTY_WINDOW_ID": "1"},
            {"TERM": "xterm-256color", "KONSOLE_VERSION": "250000"},
        )
        for environment in environments:
            with self.subTest(environment=environment), self.server() as s:
                t = self.open(s, protocol="kitty", environment=environment)
                try:
                    self.wait(t, "Apelido")

                    def fills_avatar():
                        frames = [
                            p
                            for h, p in t.stream.kitty_frames
                            if h.get(b"s") == b"48" and h.get(b"v") == b"48"
                        ]
                        return bool(frames) and all(
                            frames[-1][i * 4] > 230
                            and frames[-1][i * 4 + 1] < 25
                            and frames[-1][i * 4 + 2] < 25
                            for i in range(48 * 48)
                        )

                    self.until(t, fills_avatar)
                    t.send("\t")
                    self.wait(t, "Foco: Anexar")
                    t.send("\t")
                    self.wait(t, "Foco: Mensagem")
                    self.until(t, fills_avatar)
                    t.send("\x1b[Z\x1b[Z")
                    self.wait(t, "Foco: Histórico")
                    self.until(t, fills_avatar)
                    self.assertEqual(sum("/users/profile_batch" in r[1] for r in s.requests), 1)
                finally:
                    self.assertTrue(t.close())

    def test_webhook_avatars_load_concurrently_and_reuse_completed_downloads(self):
        with self.server(count=6) as s:
            for i, m in enumerate(s.messages):
                m.update(
                    author_id=None,
                    webhook_id=uuid(900 + i),
                    content=f"App message {i}",
                    external_author={
                        "source": "webhook",
                        "id": uuid(900 + i),
                        "name": f"App {i}",
                        "avatar_url": "/webhooks/" + uuid(900 + i) + "/avatar",
                    },
                )
            route = s.api_handler
            release = threading.Event()
            lock = threading.Lock()
            active = peak = reads = 0

            def avatar(method, path, body):
                nonlocal active, peak, reads
                if "/webhooks/" in path and path.endswith("/avatar"):
                    with lock:
                        active += 1
                        peak = max(peak, active)
                        reads += 1
                    release.wait(6)
                    with lock:
                        active -= 1
                    return 200, self.png, None, None, "image/png"
                return route(method, path, body)

            s.api_handler = avatar
            t = self.open(s, protocol="kitty")
            try:
                t.resize(48, 100)
                self.wait(t, "App 5 [APP]")
                self.until(t, lambda: peak == 4, timeout=3)
                self.assertEqual(reads, 4)
                release.set()
                self.until(t, lambda: reads == 6)
                self.until(t, lambda: len(self.avatar_positions(t)) == 6)
                self.assertLessEqual(peak, 4)
                t.click(8, 4)
                self.wait(t, "Conteúdo do outro canal")
                t.click(7, 4)
                self.wait(t, "App 5 [APP]")
                t.read(0.3)
                self.assertEqual(reads, 6)
                self.assertTrue(all(not r[2] for r in s.requests if "/webhooks/" in r[1]))
            finally:
                release.set()
                self.assertTrue(t.close())

    def test_cancelled_webhook_avatar_downloads_resume_on_return(self):
        with self.server(count=4) as s:
            for i, m in enumerate(s.messages):
                m.update(
                    author_id=None,
                    webhook_id=uuid(900 + i),
                    external_author={
                        "source": "webhook",
                        "id": uuid(900 + i),
                        "name": f"App {i}",
                        "avatar_url": "/webhooks/" + uuid(900 + i) + "/avatar",
                    },
                )
            route = s.api_handler
            release = threading.Event()
            lock = threading.Lock()
            reads = active = 0

            def avatar(method, path, body):
                nonlocal reads, active
                if "/webhooks/" in path and path.endswith("/avatar"):
                    with lock:
                        reads += 1
                        active += 1
                    release.wait(6)
                    with lock:
                        active -= 1
                    return 200, self.png, None, None, "image/png"
                return route(method, path, body)

            s.api_handler = avatar
            t = self.open(s, protocol="kitty")
            try:
                t.resize(40, 100)
                self.wait(t, "App 0 [APP]")
                self.until(t, lambda: reads == 4)
                t.click(8, 4)
                self.wait(t, "Conteúdo do outro canal")
                release.set()
                self.until(t, lambda: active == 0)
                t.click(7, 4)
                self.wait(t, "App 0 [APP]")
                self.until(t, lambda: reads == 8)
                self.until(t, lambda: len(self.avatar_positions(t)) == 4)
            finally:
                release.set()
                self.assertTrue(t.close())

    def test_slow_webhook_avatar_does_not_hold_ready_avatars(self):
        with self.server(count=4) as s:
            slow = "/webhooks/" + uuid(900) + "/avatar"
            for i, m in enumerate(s.messages):
                m.update(
                    author_id=None,
                    webhook_id=uuid(900 + i),
                    external_author={
                        "source": "webhook",
                        "id": uuid(900 + i),
                        "name": f"App {i}",
                        "avatar_url": "/webhooks/" + uuid(900 + i) + "/avatar",
                    },
                )
            route = s.api_handler
            release = threading.Event()
            started = threading.Event()

            def avatar(method, path, body):
                if "/webhooks/" in path and path.endswith("/avatar"):
                    if path.endswith(slow):
                        started.set()
                        release.wait(6)
                    return 200, self.png, None, None, "image/png"
                return route(method, path, body)

            s.api_handler = avatar
            t = self.open(s, protocol="kitty")
            try:
                t.resize(40, 100)
                self.wait(t, "App 0 [APP]")
                self.until(t, started.is_set)
                self.until(t, lambda: len(self.avatar_positions(t)) == 3, timeout=3)
                self.assertFalse(release.is_set())
                release.set()
                self.until(t, lambda: len(self.avatar_positions(t)) == 4)
            finally:
                release.set()
                self.assertTrue(t.close())

    def test_webhook_app_name_badge_and_shared_animated_avatar(self):
        with self.server(count=2) as s:
            reference = "/webhooks/" + uuid(900) + "/avatar"
            for m in s.messages:
                m.update(
                    author_id=None,
                    webhook_id=uuid(900),
                    external_author={
                        "source": "webhook",
                        "id": uuid(900),
                        "name": "Build Bot",
                        "avatar_url": reference,
                    },
                )
            route = s.api_handler

            def avatar(method, path, body):
                if path.endswith(reference):
                    return 200, self.gif, None, None, "image/gif"
                return route(method, path, body)

            s.api_handler = avatar
            t = self.open(s, protocol="kitty")
            try:
                self.wait(t, "Build Bot [APP]")
                self.assertEqual(sum("Build Bot [APP]" in row for row in t.screen.display), 1)
                self.assertNotIn("Autor indisponível", "\n".join(t.screen.display))

                def variants():
                    frames = {}
                    for h, p in t.stream.kitty_frames:
                        if h.get(b"s") == b"48" and h.get(b"v") == b"48":
                            frames.setdefault(h[b"i"], set()).add(p)
                    return frames

                self.until(t, lambda: any(len(p) > 1 for p in variants().values()))
                app_ids = set(variants())
                calls = [r for r in s.requests if r[1].endswith(reference)]
                self.assertEqual(len(calls), 1)
                self.assertFalse(calls[0][2])
                self.assertEqual(s.summary_calls, 0)
                self.assertFalse(any("/users/profile_batch" in r[1] for r in s.requests))
                t.send("x")
                t.read(0.15)
                self.assertEqual(sum(r[1].endswith(reference) for r in s.requests), 1)
                t.click(8, 4)
                self.wait(t, "Conteúdo do outro canal")
                before = len(t.stream.kitty_frames)
                t.read(0.3)
                self.assertFalse(any(h[b"i"] in app_ids for h, p in t.stream.kitty_frames[before:]))
            finally:
                self.assertTrue(t.close())

    def test_consecutive_author_messages_share_one_header_and_avatar(self):
        with self.server(count=3, realtime=True) as s:
            for i, m in enumerate(s.messages):
                m.update(author_id=uuid(21), content=f"Grupo {i + 1}")
            t = self.open(s, protocol="kitty")
            try:
                t.resize(40, 100)
                self.wait(t, "Grupo 3")
                self.wait(t, "Apelido")
                self.until(t, lambda: bool(s.peers))
                self.until(
                    t,
                    lambda: any(
                        h.get(b"s") == b"48" and h.get(b"v") == b"48"
                        for h, p in t.stream.kitty_frames
                    ),
                )
                avatar_ids = {
                    h[b"i"]
                    for h, p in t.stream.kitty_frames
                    if h.get(b"s") == b"48" and h.get(b"v") == b"48"
                }
                self.assertEqual(
                    len({p[b"p"] for p in t.stream.placements if p[b"i"] in avatar_ids}), 1
                )
                self.assertEqual(sum("Apelido" in row for row in t.screen.display), 1)
                positions = [
                    next(i for i, row in enumerate(t.screen.display) if f"Grupo {n}" in row)
                    for n in (1, 2, 3)
                ]
                self.assertEqual(positions, list(range(positions[0], positions[0] + 3)))
                name_x = next(row.index("Apelido") for row in t.screen.display if "Apelido" in row)
                self.assertTrue(
                    all(t.screen.display[row].index("Grupo") == name_x for row in positions)
                )
                event = dict(
                    s.message(4),
                    type="message",
                    author_id=uuid(21),
                    content="Grupo 4",
                    created_at=datetime.now(timezone.utc).isoformat(),
                )
                s.emit(event)
                self.wait(t, "Grupo 4")
                self.assertEqual(sum("Apelido" in row for row in t.screen.display), 1)
                s.emit(
                    {
                        "type": "message_edit",
                        "id": event["id"],
                        "channel_id": CHANNEL,
                        "content": "Grupo 4 editado",
                        "edited_at": datetime.now(timezone.utc).isoformat(),
                    }
                )
                self.wait(t, "Grupo 4 editado")
                self.until(t, lambda: sum("editado" in row for row in t.screen.display) == 2)
                self.assertEqual(sum("Apelido" in row for row in t.screen.display), 2)
                event = dict(
                    s.message(5),
                    type="message",
                    author_id=uuid(23),
                    content="Outro autor",
                    created_at=datetime.now(timezone.utc).isoformat(),
                )
                s.emit(event)
                self.wait(t, "Outro autor")
                self.wait(t, "Autor 7")
                self.assertEqual(sum("Apelido" in row for row in t.screen.display), 2)
            finally:
                self.assertTrue(t.close())

    def test_deleting_intervening_author_joins_neighboring_groups(self):
        with self.server(count=3, realtime=True) as s:
            for i, m in enumerate(s.messages):
                m.update(author_id=uuid(23) if i == 1 else uuid(21), content=f"Bloco {i + 1}")
            t = self.open(s, protocol="kitty")
            try:
                t.resize(40, 100)
                self.wait(t, "Bloco 1")
                self.wait(t, "Bloco 3")
                self.until(t, lambda: sum("Apelido" in row for row in t.screen.display) == 2)
                s.emit({"type": "message_delete", "id": s.messages[1]["id"], "channel_id": CHANNEL})
                self.until(t, lambda: "Bloco 2" not in "\n".join(t.screen.display))
                self.assertEqual(sum("Apelido" in row for row in t.screen.display), 1)
                rows = [
                    i
                    for i, row in enumerate(t.screen.display)
                    if "Bloco 1" in row or "Bloco 3" in row
                ]
                self.assertEqual(rows[1] - rows[0], 1)
            finally:
                self.assertTrue(t.close())

    def test_webhook_display_identity_change_starts_another_group(self):
        with self.server(count=3) as s:
            for i, m in enumerate(s.messages):
                m.update(
                    author_id=None,
                    webhook_id=uuid(900),
                    content=f"App texto {i + 1}",
                    external_author={
                        "source": "webhook",
                        "id": "same-id",
                        "name": "App A" if i < 2 else "App B",
                    },
                )
            t = self.open(s, protocol="kitty")
            try:
                t.resize(40, 100)
                self.wait(t, "App texto 3")
                self.assertEqual(sum("App A [APP]" in row for row in t.screen.display), 1)
                self.assertEqual(sum("App B [APP]" in row for row in t.screen.display), 1)
                rows = [
                    next(i for i, row in enumerate(t.screen.display) if f"App texto {n}" in row)
                    for n in (1, 2, 3)
                ]
                self.assertEqual(rows[1] - rows[0], 1)
                self.assertEqual(rows[2] - rows[1], 3)
                self.assertFalse(any("/users/profile_batch" in r[1] for r in s.requests))
            finally:
                self.assertTrue(t.close())

    def test_grouping_survives_prepend_scroll_and_resize(self):
        with self.server(count=130) as s:
            for i, m in enumerate(s.messages):
                m.update(author_id=uuid(21), content=f"Agrupada {i + 1:03d}", created_at=STAMP)
            t = self.open(s, protocol="kitty")
            try:
                self.wait(t, "Agrupada 130")
                t.send("\x1bOH")
                self.wait(t, "Agrupada 031")
                self.wait(t, "Apelido")
                self.assertEqual(sum("Apelido" in row for row in t.screen.display), 1)
                # Wait for the visible header's profile request to finish;
                # pagination intentionally waits while a history read is busy.
                self.until(
                    t,
                    lambda: any(
                        h.get(b"s") == b"48" and h.get(b"v") == b"48"
                        for h, p in t.stream.kitty_frames
                    ),
                )
                t.send("\x1bOA")
                self.until(t, lambda: s.older_calls == 1)
                self.until(t, lambda: "Agrupada 031" in t.screen.display[8])
                self.assertNotIn("Apelido", "\n".join(t.screen.display[8:15]))
                t.send("\x1bOH")
                self.wait(t, "Agrupada 001")
                self.wait(t, "Apelido")
                self.assertEqual(sum("Apelido" in row for row in t.screen.display), 1)
                t.resize(30, 90)
                self.wait(t, "Agrupada 001")
                self.assertEqual(sum("Apelido" in row for row in t.screen.display), 1)
                t.send("\x1bOF")
                self.wait(t, "Agrupada 130")
                self.assertEqual(s.older_calls, 1)
                self.assertFalse(
                    any("/channels/" + uuid(11) + "/messages" in r[1] for r in s.requests)
                )
            finally:
                self.assertTrue(t.close())

    def test_webhook_attribution_arrives_over_websocket(self):
        with self.server(realtime=True) as s:
            route = s.api_handler
            reference = "/webhooks/" + uuid(900) + "/avatar"

            def avatar(method, path, body):
                if path.endswith(reference):
                    return 200, self.png, None, None, "image/png"
                return route(method, path, body)

            s.api_handler = avatar
            t = self.open(s, protocol="kitty")
            try:
                self.wait(t, "Mensagem 0001")
                self.until(t, lambda: bool(s.peers))
                event = dict(
                    s.message(2),
                    type="message",
                    author_id="",
                    webhook_id=uuid(900),
                    content="Evento do app",
                    created_at=datetime.now(timezone.utc).isoformat(),
                    external_author={
                        "source": "discord",
                        "id": "external-id",
                        "name": "Deploy App",
                        "avatar_url": reference,
                    },
                )
                s.emit(event)
                self.wait(t, "Deploy App [APP]")
                self.wait(t, "Evento do app")
                self.until(t, lambda: any(r[1].endswith(reference) for r in s.requests))
                self.assertFalse(next(r for r in s.requests if r[1].endswith(reference))[2])
                self.assertEqual(s.message_count, 0)
            finally:
                self.assertTrue(t.close())

    def test_webhook_avatar_failure_keeps_name_badge_and_does_not_retry(self):
        for status in (401, 403, 404, 429, 500):
            with self.subTest(status=status), self.server() as s:
                reference = "/webhooks/" + uuid(900) + "/avatar"
                s.messages[0].update(
                    author_id=None,
                    webhook_id=uuid(900),
                    external_author={
                        "source": "webhook",
                        "id": uuid(900),
                        "name": "Build Bot",
                        "avatar_url": reference,
                    },
                )
                route = s.api_handler

                def fail(method, path, body):
                    if path.endswith(reference):
                        return status, None
                    return route(method, path, body)

                s.api_handler = fail
                t = self.open(s, protocol="kitty")
                try:
                    self.wait(t, "Build Bot [APP]")

                    def reads():
                        return sum(r[1].endswith(reference) for r in s.requests)

                    self.until(t, lambda: reads() == 1)
                    t.read(0.25)
                    t.send("x")
                    t.read(0.15)
                    self.assertEqual(reads(), 1)
                    self.assertEqual(s.refresh_count, 0)
                    self.assertNotIn("Autor indisponível", "\n".join(t.screen.display))
                finally:
                    self.assertTrue(t.close())


if __name__ == "__main__":
    unittest.main()
