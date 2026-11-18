"""Ctrl+S → Acessibilidade: contrast, colors and focus cues with synthetic servers."""

from test_context import main as test_main

import json
from pathlib import Path
import re
import tempfile
import time
import unittest
from history_mock import HistoryMock
from terminal_support import Terminal, wait_for_screen

RIGHT, DOWN = "\x1bOC", "\x1bOB"


class AccessibilityTerminal(unittest.TestCase):
    def screen(self, terminal):
        return "\n".join(terminal.screen.display)

    def wait(self, terminal, text, timeout=8):
        deadline = time.monotonic() + timeout
        while text not in self.screen(terminal) and time.monotonic() < deadline:
            terminal.read(0.03)
        self.assertIn(text, self.screen(terminal))

    def gone(self, terminal, text):
        wait_for_screen(terminal, lambda: text not in self.screen(terminal), f"{text!r} stayed")

    def open(self, server, environment=None):
        self.home = tempfile.TemporaryDirectory(prefix="papo-access-")
        self.addCleanup(self.home.cleanup)
        self.config = Path(self.home.name, "papo-tui", "config.json")
        terminal = Terminal(environment={"XDG_CONFIG_HOME": self.home.name, **(environment or {})})
        self.addCleanup(lambda: terminal.close() if terminal.process.poll() is None else None)
        terminal.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(terminal, "Mensagem 0250")
        return terminal

    def cell(self, terminal, text):
        """The first cell of text on screen."""
        for y, line in enumerate(terminal.screen.display):
            if text in line:
                return terminal.screen.buffer[y][len(line[: line.index(text)])]
        self.fail(f"{text!r} is not on screen")

    def accessibility(self, terminal):
        terminal.send("\x13")
        self.wait(terminal, "Configurações")
        terminal.send(RIGHT * 4)
        self.wait(terminal, "Cores dos cargos")
        terminal.send(DOWN)

    @staticmethod
    def text_color(terminal):
        """RGB last given to the text color (index 243) with OSC 4, as xterm-256color
        terminals redefine the palette instead of sending RGB to each cell."""
        found = re.findall(rb"\x1b\]4;243;rgb:([0-9A-Fa-f/]+)", bytes(terminal.output))
        return found[-1].decode().upper() if found else None

    def save(self, terminal):
        terminal.send("\t\r")
        self.gone(terminal, "Configurações")

    def stored(self):
        return json.loads(self.config.read_text(encoding="utf-8"))["accessibility"]

    def test_high_contrast_and_cues_apply_and_are_saved(self):
        with HistoryMock() as server:
            t = self.open(server)
            # #e4e6eb after the 0..1000 round trip of ncurses color components.
            self.assertEqual(self.text_color(t), "E3/E6/EB")
            self.accessibility(t)
            t.send(RIGHT)
            self.wait(t, "Contraste: < Alto >")
            self.wait(t, "Alto usa fundo preto ou branco puro")
            # The focused item adds bold and underline once cues are on.
            t.send(DOWN + DOWN + " ")
            self.wait(t, "[x] Indicadores além da cor")
            self.save(t)
            self.assertEqual(
                self.stored(),
                {
                    "contrast": "high",
                    "colorblind": False,
                    "cues": True,
                    "role_colors": "original",
                    "screen_reader": False,
                    "symbols": "icons",
                    "message_emoji": "images",
                    "borders": "lines",
                    "visual_alert": "off",
                    "notice_duration": "short",
                    "mouse": True,
                    "confirm_exit": False,
                },
            )
            wait_for_screen(
                t,
                lambda: self.text_color(t) == "FF/FF/FF",
                "High contrast did not change the text color",
            )
            self.accessibility(t)
            wait_for_screen(
                t,
                lambda: (
                    self.cell(t, "Contraste: < Alto >").bold
                    and self.cell(t, "Contraste: < Alto >").underscore
                ),
                "The focused item has no bold underline",
            )
            t.send("\x1b")
            self.gone(t, "Configurações")

    def test_without_colors_and_no_color_use_the_terminal_colors(self):
        with HistoryMock() as server:
            t = self.open(server)
            self.assertNotEqual(self.cell(t, "Mensagem 0250").fg, "default")
            self.accessibility(t)
            t.send(RIGHT + RIGHT)
            self.wait(t, "Contraste: < Sem cores >")
            self.save(t)
            self.assertEqual(self.stored()["contrast"], "none")
            wait_for_screen(
                t,
                lambda: self.cell(t, "Mensagem 0250").fg == "default",
                "Colors stayed on",
            )
            # The focused item is reverse video instead of a colored fill.
            self.accessibility(t)
            wait_for_screen(
                t,
                lambda: self.cell(t, "Contraste: < Sem cores >").reverse,
                "Focus is not shown without colors",
            )
        with HistoryMock() as server:
            t = self.open(server, {"NO_COLOR": "1"})
            wait_for_screen(
                t,
                lambda: self.cell(t, "Mensagem 0250").fg == "default",
                "NO_COLOR kept the colors",
            )
            # The saved choice is still Padrão.
            self.accessibility(t)
            self.wait(t, "Contraste: < Padrão >")

    def choose(self, terminal, downs):
        """Opens Acessibilidade, toggles the item that many rows below Contraste, saves."""
        self.accessibility(terminal)
        terminal.send(DOWN * downs + " ")
        self.save(terminal)

    def test_screen_reader_cursor_follows_the_focused_item(self):
        with HistoryMock() as server:
            t = self.open(server)
            self.choose(t, 4)
            self.assertTrue(self.stored()["screen_reader"])
            # Settings: the cursor rests on the focused row, visible.
            self.accessibility(t)
            row = next(y for y, line in enumerate(t.screen.display) if "Contraste:" in line)
            wait_for_screen(
                t,
                lambda: not t.screen.cursor.hidden and t.screen.cursor.y == row,
                "The cursor is not on the focused settings row",
            )
            t.send(DOWN * 4)
            row = next(y for y, line in enumerate(t.screen.display) if "Modo leitor" in line)
            wait_for_screen(
                t,
                lambda: not t.screen.cursor.hidden and t.screen.cursor.y == row,
                "The cursor did not follow the focus",
            )
            t.send("\x1b")
            self.gone(t, "Configurações")
            # Icons became text, and loading spinners stand still.
            self.wait(t, "[+]")
            self.assertNotIn("📎", self.screen(t))

    @staticmethod
    def drawn(terminal, row):
        """The characters the application wrote on a row. ``display`` writes block
        tabs as markers too, so it cannot tell them from real brackets."""
        cells = terminal.screen.buffer[row]
        return "".join(cells[x].data for x in range(terminal.screen.columns))

    def tab_rows(self, terminal):
        """The server tabs and the row of the Canais/DMs tabs, as drawn."""
        lists = next(
            y
            for y, line in enumerate(terminal.screen.display)
            if "Canais" in line and "DMs" in line
        )
        return self.drawn(terminal, 0), self.drawn(terminal, lists)

    def test_screen_reader_writes_the_tabs_between_brackets(self):
        with HistoryMock() as server:
            t = self.open(server)
            # By default the tabs are blocks of fill: no marker is written.
            servers, lists = self.tab_rows(t)
            self.assertIn(" Comunidade ", servers)
            self.assertIn(" + Adicionar ", servers)
            self.assertNotIn("[", servers + lists)
            self.choose(t, 4)
            self.assertTrue(self.stored()["screen_reader"])
            wait_for_screen(
                t,
                lambda: "[Comunidade]" in self.drawn(t, 0),
                "The selected server tab has no brackets",
            )
            servers, lists = self.tab_rows(t)
            self.assertIn("[+ Adicionar]", servers)
            self.assertIn("[Canais]", lists)
            self.assertIn("[DMs]", lists)
            # Turning the mode off brings the blocks back.
            self.choose(t, 4)
            self.assertFalse(self.stored()["screen_reader"])
            wait_for_screen(
                t,
                lambda: "[" not in "".join(self.tab_rows(t)),
                "The brackets stayed after the mode was turned off",
            )

    def test_text_symbols_emoji_names_and_ascii_borders(self):
        with HistoryMock() as server:
            t = self.open(server)
            self.assertIn("📎", self.screen(t))
            self.choose(t, 5)
            self.wait(t, "[+]")
            self.assertNotIn("📎", self.screen(t))
            self.choose(t, 6)
            self.choose(t, 7)
            stored = self.stored()
            self.assertEqual(
                (stored["symbols"], stored["message_emoji"], stored["borders"]),
                ("text", "names", "ascii"),
            )
            # Boxes use + - | instead of line drawing.
            wait_for_screen(
                t,
                lambda: t.screen.display[1].startswith("+-"),
                "Borders still use line drawing",
            )
            # The draft shows an emoji by its name.
            t.send("\x1bOB\n\t")
            self.wait(t, "Foco: Mensagem")
            t.send("oi 😀")
            self.wait(t, "oi :grinning:")
            self.assertNotIn("😀", self.screen(t))

    def test_quit_asks_for_its_key_again_and_mouse_can_be_off(self):
        with HistoryMock() as server:
            t = self.open(server)
            self.choose(t, 11)
            self.assertTrue(self.stored()["confirm_exit"])
            # Another key only cancels; the same key twice quits.
            t.send("\x11")
            self.wait(t, "Ctrl+Q de novo sai")
            t.send("\x1b")
            self.gone(t, "Ctrl+Q de novo sai")
            self.assertIsNone(t.process.poll())
            t.send("\x0c")
            self.wait(t, "Ctrl+L de novo sai da conta")
            t.send("\x1b")
            self.gone(t, "de novo sai da conta")
            # The mouse goes back to the terminal once it is off.
            start = len(t.output)
            self.choose(t, 10)
            self.assertEqual(self.stored()["mouse"], False)
            wait_for_screen(
                t,
                # ncurses may turn the SGR encoding off in the same sequence.
                lambda: re.search(rb"\x1b\[\?[0-9;]*1000l", bytes(t.output[start:])),
                "Mouse reporting stayed on",
            )
            t.send("\x11")
            self.wait(t, "Ctrl+Q de novo sai")
            t.send("\x11")
            deadline = time.monotonic() + 5
            while t.process.poll() is None and time.monotonic() < deadline:
                t.read(0.05)
            self.assertIsNotNone(t.process.poll())


if __name__ == "__main__":
    test_main()
