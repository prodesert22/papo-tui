"""Full R8 UI with synthetic HTTP/WS fixtures."""

import time
import unittest
from history_mock import CHANNEL
from terminal_support import Terminal


class R8Terminal(unittest.TestCase):
    def notification_target(self, server, count, index):
        server.count = count
        server.messages = [server.message(i) for i in range(1, count + 1)]
        target = server.messages[index - 1]
        server.notifications[0].update(
            message_id=target["id"],
            channel_id=target["channel_id"],
            message_content="Notificação de destino",
            author_id=target["author_id"],
        )
        return target

    def open_notification_target(self, terminal):
        terminal.send("\x1b[20~")
        self.wait(terminal, "Notificação de destino")
        terminal.send("\n")
        self.wait(terminal, "Detalhes da notificação")
        row = next(
            i for i, line in enumerate(terminal.screen.display) if "[Abrir conversa]" in line
        )
        terminal.click(row, terminal.screen.display[row].index("[Abrir conversa]") + 2)

    def assert_centered_target(self, terminal, content):
        self.wait(terminal, content)
        self.wait(terminal, "↑↓ mensagens")
        terminal.read(0.2)
        row = next(i for i, line in enumerate(terminal.screen.display) if content in line)
        # At 100x32, the history viewport is rows 8..22 and its border is x=27.
        self.assertLessEqual(abs(row - 15), 2, "Target must be centered in history")
        self.assertIn(terminal.screen.buffer[row][27].data, ("x", "│"))
        text_column = terminal.screen.display[row].index(content)
        self.assertNotEqual(
            terminal.screen.buffer[row][27].fg, terminal.screen.buffer[row][text_column].fg
        )

    def wait(self, t, text):
        end = time.monotonic() + 7
        while text not in "\n".join(t.screen.display) and time.monotonic() < end:
            t.read(0.03)
        self.assertIn(text, "\n".join(t.screen.display))

    def assert_initial_history_only(self, server, channel=CHANNEL):
        reads = [r[1] for r in server.requests if "/messages" in r[1]]
        self.assertEqual(len(reads), 1)
        self.assertIn(channel, reads[0])

    def login(self, server, **kwargs):
        before = sum("/messages" in r[1] for r in server.requests)
        t = Terminal(**kwargs)
        self.addCleanup(t.close)
        t.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(t, "[Canais]")
        self.wait(t, "[DMs]")
        self.wait(t, "Categoria")
        end = time.monotonic() + 5
        while (
            sum("/messages" in r[1] for r in server.requests) == before and time.monotonic() < end
        ):
            t.read(0.03)
        self.assertEqual(sum("/messages" in r[1] for r in server.requests), before + 1)
        t.read(0.1)
        return t
