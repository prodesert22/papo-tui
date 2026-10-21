"""Termux opens the soft keyboard only for taps received with mouse reporting off."""

from test_context import main as test_main

import time
import unittest
from history_mock import HistoryMock
from terminal_support import Terminal, editor_column, editor_region, wait_for_screen

SUSPEND = b"\x1b[?1002l\x1b[?1000l"
RESTORE = b"\x1b[?1000h\x1b[?1002h"
TERMUX = {"TERMUX_VERSION": "0.118.3"}


class TermuxKeyboard(unittest.TestCase):
    def screen(self, terminal):
        return "\n".join(terminal.screen.display)

    def wait(self, terminal, text, seconds=8):
        deadline = time.monotonic() + seconds
        while text not in self.screen(terminal) and time.monotonic() < deadline:
            terminal.read(0.03)
        self.assertIn(text, self.screen(terminal))

    def wait_reporting(self, terminal, sequence, description):
        deadline = time.monotonic() + 5
        while sequence not in terminal.output and time.monotonic() < deadline:
            terminal.read(0.03)
        self.assertIn(sequence, terminal.output)
        return time.monotonic()

    def label(self, terminal, text):
        for row, line in enumerate(terminal.screen.display):
            column = line.find(text)
            if column >= 0:
                return row, column
        raise AssertionError(f"Label not found: {text}")

    def mouse(self, terminal, row, column, code, release=False):
        terminal.send(f"\x1b[<{code};{column + 1};{row + 1}{'m' if release else 'M'}")

    def selected(self, terminal, row, column, ordinary):
        cell = terminal.screen.buffer[row][column]
        return (cell.fg, cell.bg) != (ordinary.fg, ordinary.bg)

    def open(self, server, **kwargs):
        terminal = Terminal(**kwargs)
        self.addCleanup(lambda: self.assertTrue(terminal.close()))
        self.wait(terminal, "URL do servidor")
        terminal.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(terminal, "geral")
        return terminal

    def test_composer_click_suspends_reporting_until_the_keyboard_opens(self):
        with HistoryMock(count=0) as server:
            terminal = self.open(server, environment=TERMUX)
            row, _ = editor_region(terminal)
            terminal.click(row, editor_column(terminal))
            # Termux confirms the tap a moment later and opens the keyboard
            # only while the reporting stays off.
            started = self.wait_reporting(
                terminal, SUSPEND, "Reporting was not suspended for the keyboard"
            )
            finished = self.wait_reporting(
                terminal, RESTORE, "Reporting was not restored after the keyboard"
            )
            self.assertLess(
                terminal.output.index(SUSPEND),
                terminal.output.index(RESTORE),
                "The reporting must be restored after the keyboard request",
            )
            # The window must outlive the platform double-tap timeout that
            # Termux waits before it opens the keyboard.
            self.assertGreater(finished - started, 0.5)
            self.assertLess(finished - started, 2.0)

    def test_login_field_click_suspends_reporting_until_the_keyboard_opens(self):
        terminal = Terminal(environment=TERMUX)
        self.addCleanup(lambda: self.assertTrue(terminal.close()))
        self.wait(terminal, "URL do servidor")
        wait_for_screen(
            terminal,
            lambda: terminal.screen.cursor.y > 0,
            "The login form did not place a caret",
        )
        terminal.click(terminal.screen.cursor.y, terminal.screen.cursor.x)
        self.wait_reporting(terminal, SUSPEND, "Reporting was not suspended for the keyboard")
        self.wait_reporting(terminal, RESTORE, "Reporting was not restored after the keyboard")

    def test_login_label_click_focuses_the_field_and_asks_for_the_keyboard(self):
        terminal = Terminal(environment=TERMUX)
        self.addCleanup(lambda: self.assertTrue(terminal.close()))
        self.wait(terminal, "URL do servidor")
        row, column = self.label(terminal, "Usuário (username)")
        terminal.click(row, column + 2)
        # The label row focuses the field without reaching the text buffer.
        self.assertEqual(terminal.wait_cursor(row + 1, column), (row + 1, column))
        self.wait_reporting(terminal, SUSPEND, "Reporting was not suspended for the keyboard")
        self.wait_reporting(terminal, RESTORE, "Reporting was not restored after the keyboard")

    def test_drag_keeps_the_reporting_until_the_gesture_ends(self):
        with HistoryMock(count=0) as server:
            terminal = self.open(server, environment=TERMUX)
            terminal.send("\x1bOB")
            terminal.send("abc")
            self.wait(terminal, "abc")
            row, _ = editor_region(terminal)
            start = editor_column(terminal)
            ordinary = terminal.screen.buffer[row][start]
            # The reporting must stay on during the gesture: suspending it
            # before the release loses the release and freezes the selection.
            self.mouse(terminal, row, start, 0)
            terminal.read(0.2)
            self.assertFalse(SUSPEND in terminal.output, "The reporting was suspended mid-drag")
            self.mouse(terminal, row, start + 3, 32)
            wait_for_screen(
                terminal,
                lambda: self.selected(terminal, row, start + 2, ordinary),
                "The drag did not select the text",
            )
            self.assertFalse(SUSPEND in terminal.output, "The reporting was suspended mid-drag")
            self.mouse(terminal, row, start + 3, 0, release=True)
            self.wait_reporting(terminal, SUSPEND, "Reporting was not suspended for the keyboard")

    def test_reporting_is_untouched_outside_termux(self):
        with HistoryMock(count=0) as server:
            terminal = self.open(server)
            row, _ = editor_region(terminal)
            terminal.click(row, editor_column(terminal))
            terminal.read(0.5)
            self.assertFalse(SUSPEND in terminal.output, "The reporting was suspended")


if __name__ == "__main__":
    test_main(
        1,
    )
