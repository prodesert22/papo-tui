"""Emoji picker, authenticated assets and retained pictures in a real PTY."""

import base64
from datetime import datetime, timezone
import subprocess
import time
import unittest
from urllib.parse import urlsplit, parse_qs
import terminal_media as media_tests
from history_mock import HistoryMock, STAMP, uuid
from realtime_mock import RealtimeMock


def encode_emoji_animation(gif, format):
    encoder, container = {
        "GIF": ("gif", "gif"),
        "WEBP": ("libwebp_anim", "webp"),
        "PNG": ("apng", "apng"),
    }[format]
    # This CLI generates fixtures; decoding is exercised by the client's own
    # FFmpeg libraries. Its decoder list does not describe client capabilities.
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
                    t.send("\x0b")
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
