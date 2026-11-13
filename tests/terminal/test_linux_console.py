"""The Linux console (TERM=linux) through a PTY and synthetic HTTP: focus colors, glyphs and emoji."""

from test_context import main as test_main

from test_context import argument

import tempfile
import time
import unittest
from pathlib import Path

import terminal_support as test_terminal
from history_mock import HistoryMock

BINARY = Path(argument(0))

# Glyphs of the console fonts the interface may use (Lat2-Terminus16, the
# usual default): ASCII, Latin letters, box drawing and a few symbols.
CONSOLE_SYMBOLS = set("·×—–‹›…•←↑→↓─│┌┐└┘├┤┬┴┼━█░▒■▲▼▶◀●")


def drawable(character):
    point = ord(character)
    return point < 0x250 or character in CONSOLE_SYMBOLS


class LinuxConsole(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.TemporaryDirectory(prefix="papo-console-")
        self.addCleanup(self.root.cleanup)
        self.environment = {
            "XDG_CONFIG_HOME": self.root.name,
            "XDG_STATE_HOME": str(Path(self.root.name, "state")),
        }
        previous = test_terminal.BINARY
        test_terminal.BINARY = str(BINARY.resolve())
        self.addCleanup(setattr, test_terminal, "BINARY", previous)

    def screen(self, t):
        return "\n".join(t.screen.display)

    def wait(self, t, predicate, timeout=8):
        deadline = time.monotonic() + timeout
        while not predicate() and time.monotonic() < deadline:
            t.read(0.03)
        self.assertTrue(predicate(), self.screen(t))

    def wait_text(self, t, text):
        self.wait(t, lambda: text in self.screen(t))

    def open(self, server, size=None):
        t = test_terminal.Terminal(environment=self.environment, terminal_type="linux")
        self.addCleanup(lambda: t.close() if t.process.poll() is None else None)
        if size:
            t.resize(*size)
        t.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait_text(t, "Conversar em #geral")
        return t

    def cells(self, t):
        for y in range(t.screen.lines):
            for x in range(t.screen.columns):
                yield y, x, t.screen.buffer[y][x]

    def assert_readable(self, t, where):
        """No text shares its background's color; text on a fill is plain black."""
        for y, x, cell in self.cells(t):
            if not cell.data.strip():
                continue
            self.assertNotEqual(
                cell.fg, cell.bg, f"{where}: {cell.data!r} at {y},{x} vanishes\n{self.screen(t)}"
            )
            if cell.bg not in ("default", "black"):
                self.assertEqual(
                    (cell.fg, cell.bold),
                    ("black", False),
                    f"{where}: {cell.data!r} at {y},{x} on {cell.bg}\n{self.screen(t)}",
                )

    def assert_drawable(self, t, where):
        missing = {cell.data for _, _, cell in self.cells(t) if not all(map(drawable, cell.data))}
        self.assertFalse(missing, f"{where}: no console glyph for {missing}\n{self.screen(t)}")

    def test_focus_fill_keeps_black_text_at_every_stop(self):
        with HistoryMock(count=6) as server:
            t = self.open(server)
            try:
                self.assert_readable(t, "server tabs")
                filled = 0
                for stop in range(12):
                    t.send("\t")
                    t.read(0.2)
                    self.assert_readable(t, f"Tab {stop + 1}")
                    filled += any(cell.bg == "brown" for _, _, cell in self.cells(t))
                self.assertGreaterEqual(filled, 8, "focused buttons lost their fill")
                t.send("\x13")
                self.wait_text(t, "Configurações")
                self.assert_readable(t, "settings")
            finally:
                self.assertTrue(t.close())

    def test_interface_draws_only_console_glyphs(self):
        with HistoryMock(count=6) as server:
            t = self.open(server, size=(30, 120))
            try:
                screen = self.screen(t)
                for label in ("[/]", "[Fix]", "[@]", "[+]", "[:)]", "◀ voz", "[*]"):
                    self.assertIn(label, screen)
                self.wait_text(t, "Tab / Shift+Tab: mudar foco")
                self.assertNotIn("Alt +", self.screen(t))
                self.assert_drawable(t, "conversation")
                t.send("\x07")
                self.wait_text(t, "Ajuda do contexto")
                self.assert_drawable(t, "help")
                t.send("\x1b")
                t.send("\x13")
                self.wait_text(t, "Configurações")
                self.assert_drawable(t, "settings")
            finally:
                self.assertTrue(t.close())

    def test_emojis_show_as_names_and_send_unchanged(self):
        server = HistoryMock(count=6)
        server.messages[-1]["content"] = "Oi 😂 tudo"
        server.messages[-1]["reactions"] = [{"unicode": "😂", "emoji_id": None, "count": 2}]
        with server:
            t = self.open(server)
            try:
                self.wait_text(t, "Oi :rosto chorando de rir: tudo")
                self.assertIn("[:rosto chorando de rir: 2]", self.screen(t))
                self.assert_drawable(t, "history")
                t.send("\t" * 5)
                self.wait_text(t, "Foco: Mensagem")
                t.send("\x05")
                self.wait_text(t, "Emojis · Esc voltar")
                t.send("chorando de rir")
                self.wait(t, lambda: "rosto chorando de rir" in self.screen(t).split("Buscar:")[1])
                self.assert_drawable(t, "emoji list")
                self.assert_readable(t, "emoji list")
                t.send("\t\t\n")
                self.wait_text(t, "> :rosto chorando de rir:")
                t.send("\n")
                self.wait(t, lambda: len(server.sent_messages) == 1)
                self.assertEqual(server.sent_messages[0]["content"], "😂")
            finally:
                self.assertTrue(t.close())


if __name__ == "__main__":
    test_main()
