"""Shared steps of the suites that drive the update check and download."""

import base64
import os
import re
import time

import terminal_discovery as support

DOWN, UP, LEFT, RIGHT = "\x1bOB", "\x1bOA", "\x1bOD", "\x1bOC"
LIST = "/papo/github/releases?per_page=5"
LATEST = "/papo/github/releases/latest"


class UpdateSuite(support.R8Terminal):
    def text(self, t):
        return "\n".join(t.screen.display)

    def calls(self, hub, count, seconds=10):
        """Waits for exactly ``count`` release requests."""
        end = time.monotonic() + seconds
        while len(hub.calls) < count and time.monotonic() < end:
            time.sleep(0.03)
        self.assertEqual(len(hub.calls), count, hub.calls)

    def quiet(self, t, hub, count, seconds):
        """Proves no further release request is sent for ``seconds``."""
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            t.read(0.05)
        self.assertEqual(len(hub.calls), count, hub.calls)

    def gone(self, t, text):
        end = time.monotonic() + 7
        while text in self.text(t) and time.monotonic() < end:
            t.read(0.03)
        self.assertNotIn(text, self.text(t))

    def open_sobre(self, t):
        t.send("\x13")
        self.wait(t, "Configurações")
        t.send(LEFT)
        self.wait(t, "< Sobre >")

    def check_now(self, t):
        """From the section selector: past the lines, on the button, Enter."""
        t.send("\t\t\n")

    def click(self, t, label):
        for y, line in enumerate(t.screen.display):
            if label in line:
                t.click(y, line.index(label) + 1)
                return
        self.fail("Item absent: " + label)

    def copies(self, t):
        return [
            base64.b64decode(value).decode()
            for value in re.findall(rb"\x1b\]52;c;([A-Za-z0-9+/=]*)\x07", t.output)
        ]

    def state_file(self, t):
        return os.path.join(t.configuration.name, "state", "papo-tui", "update.json")

    def paths(self, hub):
        """The request a build of this kind sends: finals ask for the latest final."""
        self.assertTrue(all(path in (LIST, LATEST) for path, _ in hub.calls), hub.calls)
        self.assertTrue(all(cookie is None for _, cookie in hub.calls), hub.calls)

    def dump(self, t, name):
        """Keeps the screen text as evidence when PAPO_SCREEN_DUMPS names a folder."""
        folder = os.environ.get("PAPO_SCREEN_DUMPS")
        if folder:
            with open(os.path.join(folder, name + ".txt"), "w", encoding="utf-8") as f:
                f.write("\n".join(t.screen.display) + "\n")
