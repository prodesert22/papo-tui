"""GIF source of the emoji picker against a synthetic search API; no real network."""

from test_context import protocols

from test_context import main as test_main

from urllib.parse import parse_qs, urlsplit
import unittest

import terminal_media as media
from terminal_support import wait_for_screen

DOWN = "\x1bOB"
UP = "\x1bOA"
RIGHT = "\x1bOC"
CTRL_E = "\x05"


def listing(origin, names):
    pages = [
        {
            "title": f"File:{name.replace(' ', '_')}.gif",
            "index": number,
            "imageinfo": [
                {
                    "url": f"{origin}/g/full-{number}.gif",
                    "thumburl": f"{origin}/g/thumb-{number}.gif",
                    "mime": "image/gif",
                    "size": 4000,
                    "width": 16,
                    "height": 12,
                }
            ],
        }
        for number, name in enumerate(names, 1)
    ]
    return {"batchcomplete": True, "query": {"pages": pages}}


class GifPicker(unittest.TestCase):
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

    def serve(self, fail=False):
        server = self.helper.serve(media.MediaTerminal.images["png"], "image/png", embed=True)
        server.messages[0]["attachments"] = []
        parent = server.api_handler
        server.searches = []

        def route(method, path, body):
            url = urlsplit(path)
            if url.path.endswith("/gifs/api"):
                query = parse_qs(url.query)["gsrsearch"][0]
                server.searches.append(query)
                if fail:
                    return 500, {"error": "unavailable"}
                if "dog" in query:
                    return 200, listing(server.url, ["Dog trot"])
                if "nothing" in query:
                    return 200, {"batchcomplete": True}
                return 200, listing(server.url, ["Cat dance", "Cat nap", "Cat jump"])
            if "/g/" in url.path:
                return 200, media.MediaTerminal.images["gif"], None, None, "image/gif"
            return parent(method, path, body)

        server.api_handler = route
        return server

    def open_gifs(self, server, protocol):
        environment = {"PAPO_GIF_SEARCH_URL": server.url + "/gifs/api"}
        t = self.helper.open(server, protocol=protocol, environment=environment)
        t.send(CTRL_E)
        self.helper.wait(t, "[GIF]")
        return t

    def screen(self, t):
        return "\n".join(t.screen.display)

    def test_search_shows_results_and_inserts_the_link(self):
        for protocol in protocols("kitty", "sixel", "iterm2", "chafa", "light", "ultralight"):
            with self.subTest(protocol=protocol), self.serve() as server:
                t = self.open_gifs(server, protocol)
                try:
                    # Opening the picker alone never contacts the search service.
                    self.assertEqual(server.searches, [])
                    t.send(DOWN + RIGHT + RIGHT)
                    self.helper.wait(t, "Cat dance")
                    self.helper.wait(t, "Wikimedia Commons")
                    if protocol in ("kitty", "sixel", "iterm2"):
                        # The thumbnails come from the search results, not from the instance.
                        # They are requested after the results are drawn, and the
                        # history's embed already produced frames, so wait for them.
                        self.helper.settled(
                            t, lambda: any("/g/thumb-" in r[1] for r in server.requests)
                        )
                        self.helper.wait_frames(t, 1)
                    self.assertEqual(server.searches, ["filemime:image/gif"])
                    # Typing narrows the search once the pause after the last key ends.
                    t.send(UP + "dog")
                    self.helper.wait(t, "Dog trot")
                    self.assertNotIn("Cat dance", self.screen(t))
                    self.assertEqual(server.searches[-1], "dog filemime:image/gif")
                    self.assertLess(len(server.searches), 4, "typing must not search per key")
                    t.send(DOWN + DOWN)
                    t.send("\n")
                    wait_for_screen(
                        t,
                        lambda: (
                            "[GIF]" not in self.screen(t)
                            and "/g/ful" in self.screen(t)
                            and "l-1.gif" in self.screen(t)
                        ),
                        "The chosen GIF address must land in the draft and close the picker",
                    )
                finally:
                    self.assertTrue(t.close())

    def test_empty_and_failed_searches_say_so(self):
        for fail in (False, True):
            with self.subTest(fail=fail), self.serve(fail=fail) as server:
                t = self.open_gifs(server, "kitty")
                try:
                    t.send(DOWN + RIGHT + RIGHT + UP + "nothing")
                    self.helper.wait(
                        t, "A busca de GIFs falhou" if fail else "Nenhum GIF encontrado"
                    )
                    t.send("\x1b")
                    self.helper.wait(t, "Conversar em #geral")
                finally:
                    self.assertTrue(t.close())

    def test_gif_link_in_a_message_plays_in_place(self):
        cases = [
            (p, e)
            for p in protocols("kitty", "sixel", "iterm2", "chafa", "light")
            for e in ("gif", "apng", "webp")
        ]
        for protocol, extension in cases:
            with self.subTest(protocol=protocol, extension=extension), self.serve() as server:
                link = server.url + "/g/loop." + extension
                server.messages[0]["content"] = "olha isso " + link
                parent = server.api_handler

                def route(method, path, body):
                    if "/g/loop." in path:
                        return 200, media.MediaTerminal.animated_gif, None, None, "image/gif"
                    return parent(method, path, body)

                server.api_handler = route
                t = self.helper.open(
                    server,
                    protocol=protocol,
                    environment={"PAPO_GIF_SEARCH_URL": server.url + "/gifs/api"},
                )
                try:
                    self.helper.wait_frames(t, 4)
                    self.helper.settled(t, lambda: len(set(t.stream.frames)) >= 2, seconds=10)
                    self.assertEqual(sum("/g/loop." in r[1] for r in server.requests), 1)
                finally:
                    self.assertTrue(t.close())

    def test_opening_the_picker_never_contacts_the_search_service(self):
        with self.serve() as server:
            t = self.helper.open(
                server,
                protocol="kitty",
                environment={"PAPO_GIF_SEARCH_URL": server.url + "/gifs/api"},
            )
            try:
                t.send(CTRL_E)
                self.helper.wait(t, "[GIF]")
                t.send("\x1b")
                self.helper.wait(t, "Conversar em #geral")
                self.assertEqual(server.searches, [])
            finally:
                self.assertTrue(t.close())


if __name__ == "__main__":
    test_main(2)
