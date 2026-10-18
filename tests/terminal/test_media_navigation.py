"""Message media navigation with synthetic HTTP and PTY graphics responders."""

import base64
import json
from pathlib import Path
import tempfile
import unittest

import test_media_terminal as media
from test_terminal import wait_for_screen


def wait_identity(terminal, identity):
    """Wait for the properties panel id; long uuids wrap across panel rows.

    Only characters that can appear in a uuid are kept, so the box borders
    drawn between two wrapped halves of the id do not break the match.
    """
    wait_for_screen(
        terminal,
        lambda: (
            media.uuid(identity)
            in "".join(ch for ch in "\n".join(terminal.screen.display) if ch in "0123456789abcdef-")
        ),
        f"Image {identity} is not the one shown",
    )


class MediaNavigation(unittest.TestCase):
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

    def test_enter_on_embed_cover_opens_video_player(self):
        for protocol in ("kitty", "sixel", "iterm2"):
            with (
                self.subTest(protocol=protocol),
                self.helper.serve(
                    media.MediaTerminal.images["png"], "image/png", embed=True
                ) as server,
            ):
                server.messages[0]["embeds"] = [
                    dict(
                        id=media.uuid(902),
                        title="Video with cover",
                        thumbnail=dict(mime_type="image/png"),
                        video=dict(mime_type="video/mp4", url="https://example.invalid/video"),
                    )
                ]
                parent = server.api_handler

                def route(method, path, body):
                    if path.endswith("/video"):
                        return 200, media.MediaTerminal.video, None, None, "video/mp4"
                    return parent(method, path, body)

                server.api_handler = route
                t = self.helper.open(server, protocol=protocol)
                try:
                    self.helper.wait_frames(t)
                    t.send("Preserved draft\x1b[18~")
                    self.helper.wait(t, "Mídia 1/2")
                    t.send("\n")
                    self.helper.wait(t, "Reproduzindo")
                    t.send(" ")
                    self.helper.wait(t, "Pausado")
                    t.send("\x1bOC\x1bOD")
                    self.helper.wait(t, "0 / 3 s")
                    self.assertNotIn("imagens", "\n".join(t.screen.display))
                    t.send("\x1b")
                    self.helper.wait(t, "Mídia 1/2")
                    # The explicit player entry continues to work and reuses the download.
                    t.send("\x1bOC\n")
                    self.helper.wait(t, "Reproduzindo")
                    t.send("\x1b\x1b")
                    self.helper.wait(t, "Preserved draft")
                    self.assertEqual(sum(r[1].endswith("/video") for r in server.requests), 1)
                    self.assertFalse(any("example.invalid" in r[1] for r in server.requests))
                finally:
                    self.assertTrue(t.close())

    def gallery_server(self, embeds):
        server = self.helper.serve(media.MediaTerminal.images["png"], "image/png", embed=embeds)
        files = [
            (media.MediaTerminal.images["png"], "image/png"),
            (media.MediaTerminal.large_images["landscape"], "image/png"),
            (media.MediaTerminal.animated_gif, "image/gif"),
        ]
        if embeds:
            server.messages[0]["embeds"] = [
                dict(
                    id=media.uuid(901 + i), title=f"Picture {i + 1}", thumbnail=dict(mime_type=mime)
                )
                for i, (_, mime) in enumerate(files)
            ]
        else:
            server.messages[0]["attachments"] = [
                dict(
                    id=media.uuid(901 + i),
                    original_file_name=f"Picture {i + 1}",
                    mime_type=mime,
                    size_bytes=len(data),
                    moderation_status="clean",
                )
                for i, (data, mime) in enumerate(files)
            ]
        # This image belongs to another message and must never enter this gallery.
        server.messages.insert(
            0,
            dict(
                server.messages[0],
                id=media.uuid(99),
                created_at="2026-10-11T12:00:00Z",
                embeds=[],
                attachments=[dict(id=media.uuid(990), mime_type="image/png")],
            ),
        )
        # The mock only serves the first `count` messages, so the gallery message
        # is unreachable until the second one is published too.
        server.count = 2
        parent = server.api_handler

        def route(method, path, body):
            for i, (data, mime) in enumerate(files):
                if path.split("?")[0].endswith("/" + media.uuid(901 + i)):
                    if embeds:
                        # The embed endpoint only ingests the blob when the body
                        # echoes the requested id.
                        return 200, dict(
                            id=media.uuid(901 + i),
                            image_data=base64.b64encode(data).decode(),
                        )
                    return 200, data, None, None, mime
            return parent(method, path, body)

        server.api_handler = route
        return server

    def test_image_arrows_wrap_within_message_and_keep_properties(self):
        for embeds in (False, True):
            for protocol, mode in (
                ("kitty", "normal"),
                ("sixel", "normal"),
                ("iterm2", "normal"),
                (None, "normal"),
                (None, "light"),
            ):
                with (
                    self.subTest(embeds=embeds, protocol=protocol, mode=mode),
                    tempfile.TemporaryDirectory() as root,
                    self.gallery_server(embeds) as server,
                ):
                    config = Path(root, "papo-tui", "config.json")
                    config.parent.mkdir()
                    config.write_text(json.dumps(dict(version=4, profiles=[], app_mode=mode)))
                    t = self.helper.open(
                        server,
                        protocol=protocol,
                        sixel=False,
                        environment={"XDG_CONFIG_HOME": root},
                        size=(38, 100),
                    )
                    try:
                        t.send("Gallery draft\x1b[18~\n")
                        self.helper.wait(t, "1/3 ·")
                        t.send("i")
                        self.helper.wait(t, "Propriedades")
                        wait_identity(t, 901)
                        for key, number, identity in (
                            ("\x1bOC", 2, 902),
                            ("\x1bOC", 3, 903),
                            ("\x1bOC", 1, 901),
                            ("\x1bOD", 3, 903),
                            ("\x1bOD", 2, 902),
                        ):
                            t.send(key)
                            self.helper.wait(t, f"{number}/3 ·")
                            self.helper.wait(t, "Propriedades")
                            wait_identity(t, identity)
                        t.resize(30, 84)
                        self.helper.wait(t, "2/3 ·")
                        t.send("\x1b")
                        wait_for_screen(
                            t,
                            lambda: "[Info]" not in "\n".join(t.screen.display),
                            "Image viewer did not close",
                        )
                        self.helper.wait(t, "Gallery draft")
                        endpoint = "/embeds/" if embeds else "/attachments/"
                        for identity in (901, 902, 903):
                            downloads = sum(
                                endpoint + media.uuid(identity) in r[1] for r in server.requests
                            )
                            # Every gallery image is reachable and the viewer keeps
                            # reusing the shared cache. A single extra fetch is
                            # still allowed: cache maintenance may cancel a long
                            # ingest and the retry then re-requests it.
                            self.assertGreaterEqual(
                                downloads, 1, f"{endpoint}{media.uuid(identity)} was never fetched"
                            )
                            self.assertLessEqual(
                                downloads, 2, f"{endpoint}{media.uuid(identity)} was re-downloaded"
                            )
                        if mode == "light" or protocol is None:
                            self.assertEqual(t.stream.frames, [])
                    finally:
                        self.assertTrue(t.close())


if __name__ == "__main__":
    unittest.main()
