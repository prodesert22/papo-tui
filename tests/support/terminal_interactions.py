"""Message selection, replies and reactions through the actual TUI.

Reaction fixtures use distinct BMP glyphs, which fit ncurses' Windows wchar_t.
The native input suites separately verify non-BMP input and message payloads.
"""

import faulthandler
import json
import os
from pathlib import Path
import time
import unittest
from wcwidth import wcswidth
import terminal_send as product
from terminal_support import editor_region


class MessageInteractions(unittest.TestCase):
    screen = product.SendProduct.screen

    click = product.SendProduct.click

    def editor_text(self, terminal):
        def editor_visible():
            try:
                editor_region(terminal)
            except StopIteration:
                return False
            return True

        # Closing a panel can erase its title before ConPTY delivers the
        # restored editor. Observe the editor before inspecting its contents.
        self.settled(terminal, editor_visible)
        return product.SendProduct.editor_text(self, terminal)

    def wait(self, terminal, text):
        self.settled(terminal, lambda: text in self.screen(terminal), seconds=8)

    def resize(self, terminal, rows, columns):
        terminal.resize(rows, columns)
        # ConPTY can retain old text while resize output is in flight. Wait
        # for the new frame before reading positions or sending input.
        # The POSIX observer exposes the DEC border glyph as "j".
        self.settled(
            terminal, lambda: terminal.screen.buffer[rows - 1][columns - 1].data in ("j", "┘")
        )

    def setUp(self):
        if os.name == "nt":
            # Preserve the blocked call stack if a native ConPTY call stalls;
            # CTest's outer timeout otherwise kills Python without a traceback.
            faulthandler.dump_traceback_later(60)
            self.addCleanup(faulthandler.cancel_dump_traceback_later)

    def open(self, server):
        # Keep Unicode labels observable for text interaction assertions. The
        # graphics suites cover rendered icons; mode tests exercise all modes.
        # Let the app create its private configuration with native permissions.
        self._mock_server = server
        terminal = product.SendProduct.open(self, server)
        try:
            terminal.send("\x1bOQ")
            self.wait(terminal, "Configurações")
            self.wait(terminal, "(*) Normal")
            terminal.send("\t\x1bOB\n\t\n")
            # History remains visible behind settings, so its editor label
            # alone does not prove that the modal and mode reflow have closed.
            self.settled(terminal, lambda: "Configurações" not in self.screen(terminal))
            self.wait(terminal, "Conversar em #geral")
            # Saving closes the configuration file before closing the modal.
            # Polling the file during save races Windows' exclusive write handle.
            path = Path(terminal.configuration.name, "papo-tui", "config.json")
            self.assertEqual(json.loads(path.read_text(encoding="utf-8"))["app_mode"], "light")
            return terminal
        except BaseException:
            terminal.close()
            raise

    def right_click(self, terminal, text):
        self.wait(terminal, text)
        for row, line in enumerate(terminal.screen.display):
            if text in line:
                column = wcswidth(line[: line.index(text)]) + 1
                terminal.send(f"\x1b[<2;{column + 1};{row + 1}M\x1b[<2;{column + 1};{row + 1}m")
                return
        self.fail("Missing message: " + text)

    def settled(self, terminal, predicate, seconds=6):
        deadline = time.monotonic() + seconds
        while not predicate() and time.monotonic() < deadline:
            terminal.read(0.03)
        self.assertTrue(predicate(), self.screen(terminal))
