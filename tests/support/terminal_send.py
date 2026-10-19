"""R6 through the actual login/conversation screen with synthetic HTTP data."""

import os
import time
import unittest
from terminal_support import Terminal, editor_region
from wcwidth import wcswidth


class SendProduct(unittest.TestCase):
    def screen(self, terminal):
        return "\n".join(terminal.screen.display)

    def wait(self, terminal, text):
        deadline = time.monotonic() + 8
        while text not in self.screen(terminal) and time.monotonic() < deadline:
            terminal.read(0.03)
        self.assertIn(text, self.screen(terminal))

    def click(self, terminal, text):
        self.wait(terminal, text)
        for row, line in enumerate(terminal.screen.display):
            if text in line:
                terminal.click(row, wcswidth(line[: line.index(text)]) + 1)
                return
        self.fail("Missing control: " + text)

    def open(self, server, *, environment=None):
        terminal = Terminal(environment=environment)
        if os.name == "nt":
            # Register before login assertions, which may fail before returning
            # the terminal to a caller's try/finally block.
            self.addCleanup(terminal.close)
        terminal.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(terminal, "geral")
        terminal.send("\x1bOB\n")
        self.wait(terminal, "# geral · Histórico")
        self.wait(terminal, "Conversar em #geral")
        return terminal

    def editor_text(self, terminal):
        # Editor input is separated from message history and status lines.
        rows = terminal.screen.display
        first, end = editor_region(terminal)
        return "\n".join(rows[first:end])
