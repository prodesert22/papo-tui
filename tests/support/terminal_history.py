"""R5 through ncurses in a pseudoterminal, using synthetic HTTP data."""

import time
import unittest
from terminal_support import Terminal, ObservedScreen
from wcwidth import wcswidth


class SidebarObserver(ObservedScreen):
    """Catch transient sidebar changes, even if a later write restores it."""

    def __init__(self, columns, lines):
        super().__init__(columns, lines)
        self.watch = False
        self.sidebar_changes = []

    def sidebar(self):
        return tuple(
            tuple(self.buffer[y][x] for x in range(1, 25)) for y in range(5, self.lines - 3)
        )

    def observe(self, method, *args, **kwargs):
        before = self.sidebar() if self.watch else None
        result = method(*args, **kwargs)
        if self.watch and self.sidebar() != before:
            self.sidebar_changes.append(method.__name__)
        return result

    def draw(self, *args, **kwargs):
        return self.observe(super().draw, *args, **kwargs)

    def erase_in_line(self, *args, **kwargs):
        return self.observe(super().erase_in_line, *args, **kwargs)

    def erase_in_display(self, *args, **kwargs):
        return self.observe(super().erase_in_display, *args, **kwargs)

    def insert_lines(self, *args, **kwargs):
        return self.observe(super().insert_lines, *args, **kwargs)

    def delete_lines(self, *args, **kwargs):
        return self.observe(super().delete_lines, *args, **kwargs)

    def index(self, *args, **kwargs):
        return self.observe(super().index, *args, **kwargs)

    def reverse_index(self, *args, **kwargs):
        return self.observe(super().reverse_index, *args, **kwargs)

    def scroll_up(self, *args, **kwargs):
        return self.observe(super().scroll_up, *args, **kwargs)

    def scroll_down(self, *args, **kwargs):
        return self.observe(super().scroll_down, *args, **kwargs)


class ConversationObserver(SidebarObserver):
    def sidebar(self):
        return tuple(
            tuple(self.buffer[y][x] for x in range(26, self.columns - 1))
            for y in range(5, self.lines - 3)
        )


class HistoryTerminal(unittest.TestCase):
    def wait(self, terminal, text):
        deadline = time.monotonic() + 8
        while text not in "\n".join(terminal.screen.display) and time.monotonic() < deadline:
            terminal.read(0.03)
        self.assertIn(text, "\n".join(terminal.screen.display))

    def open(self, server, terminal_type="xterm-256color"):
        t = Terminal(terminal_type=terminal_type)
        t.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(t, "geral")
        self.assertNotIn("URL do servidor", "\n".join(t.screen.display))
        self.wait(t, "Conversar em #geral")
        t.send("\x1bOB\n")  # tabs -> first channel -> history
        self.wait(t, "Conversar em #geral")
        return t

    def click_text(self, terminal, text):
        for row, line in enumerate(terminal.screen.display):
            if text in line:
                terminal.click(row, wcswidth(line[: line.index(text)]) + min(2, wcswidth(text) - 1))
                return
        self.fail("Control absent: " + text)
