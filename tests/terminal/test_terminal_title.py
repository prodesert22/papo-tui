"""Terminal window titles from synthetic conversations and unread mentions."""

from test_context import main as test_main

import os
import re
import unittest

from history_mock import CHANNEL, OTHER, uuid
from r8_mock import AUTHOR, R8Mock
from terminal_support import Terminal, wait_for_screen
from wcwidth import wcswidth


class TerminalTitle(unittest.TestCase):
    def wait(self, terminal, text):
        wait_for_screen(
            terminal,
            lambda: text in "\n".join(terminal.screen.display),
            "Missing text: " + text,
            seconds=8,
        )

    def title(self, terminal, expected):
        wait_for_screen(
            terminal,
            lambda: terminal.screen.title == expected,
            "Unexpected terminal title: " + expected,
            seconds=8,
        )

    def click(self, terminal, text):
        self.wait(terminal, text)
        row, line = next(
            (row, line) for row, line in enumerate(terminal.screen.display) if text in line
        )
        terminal.click(row, wcswidth(line[: line.index(text)]) + 1)

    def open(self, server):
        terminal = Terminal()
        self.addCleanup(self.close, terminal)
        terminal.resize(36, 120)
        self.title(terminal, "Papo TUI")
        terminal.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(terminal, "Conversar em #")
        return terminal

    def close(self, terminal):
        if not getattr(terminal, "title_test_closed", False):
            self.assertTrue(terminal.close())
            terminal.title_test_closed = True

    def test_conversations_mentions_reading_logout_and_title_stack(self):
        with R8Mock(count=3) as server:
            server.notifications[1]["message_content"] = "Olá @mention(<@" + uuid(1) + ">)"
            server.notifications[2]["message_content"] = "Aviso @everyone"
            terminal = self.open(server)
            self.title(terminal, "geral | Comunidade (2)")
            self.click(terminal, "# outro")
            self.title(terminal, "outro | Comunidade (2)")
            next(c for c in server.channels if c["id"] == OTHER)["name"] = "novidades 🐧"
            server.emit(
                dict(type="channel_update", channel_id=OTHER, name="novidades 🐧", position=3)
            )
            self.title(terminal, "novidades 🐧 | Comunidade (2)")
            self.click(terminal, "[DMs]")
            self.click(terminal, "Bia ·")
            self.title(terminal, "DM com Bia | Comunidade (2)")
            terminal.send("\x04\n")
            self.wait(terminal, "Detalhes da notificação")
            self.title(terminal, "DM com Bia | Comunidade (1)")
            terminal.send("\x1b\x1bOB\n")
            self.wait(terminal, "Detalhes da notificação")
            self.title(terminal, "DM com Bia | Comunidade")
            wait_for_screen(
                terminal, lambda: len(server.marked) == 2, "Notification reads did not finish"
            )
            terminal.send("\x1b\x1b\x0c")
            self.wait(terminal, "URL do servidor")
            self.title(terminal, "Papo TUI")
            self.close(terminal)
            if os.name != "nt":
                # ConPTY consumes/translates window controls before observation.
                self.assertEqual(bytes(terminal.output).count(b"\x1b[22;2t"), 1)
                self.assertEqual(bytes(terminal.output).count(b"\x1b[23;2t"), 1)

    def test_remote_names_cannot_terminate_title_or_emit_controls(self):
        with R8Mock(count=0) as server:
            server.name = "Servidor\x1b]2;FAKE\a\x9c 🐧"
            next(c for c in server.channels if c["id"] == CHANNEL)["name"] = (
                "geral\n\x1b]52;c;TITLE-INJECT\a\x9b界"
            )
            terminal = self.open(server)
            self.title(terminal, "geral ]52;c;TITLE-INJECT界 | Servidor]2;FAKE 🐧")
            self.assertNotIn(b"\x1b]52;c;TITLE-INJECT", terminal.output)
            self.assertNotIn(b"\x1b]2;FAKE", terminal.output)
            title_pattern = rb"\x1b](?:0|2);(.*?)(?:\x07|\x1b\\)"
            payloads = re.findall(title_pattern, bytes(terminal.output), re.DOTALL)
            self.assertTrue(payloads)
            for payload in payloads:
                self.assertFalse(any(ord(c) < 32 or 127 <= ord(c) < 160 for c in payload.decode()))
            count = len(payloads)
            terminal.resize(30, 100)
            self.wait(terminal, "Conversar em #")
            terminal.read(0.3)
            if os.name != "nt":
                self.assertEqual(
                    len(re.findall(title_pattern, bytes(terminal.output), re.DOTALL)), count
                )

    def test_long_unicode_names_keep_utf8_valid_and_title_bounded(self):
        with R8Mock(count=0) as server:
            server.name = "🐧" * 31
            next(c for c in server.channels if c["id"] == CHANNEL)["name"] = "界" * 500
            terminal = self.open(server)
            self.title(terminal, "界" * 32 + " | " + "🐧" * 24)
            self.assertLess(len(terminal.screen.title.encode()), 255)

    def test_inactive_instance_mentions_update_without_prefetching_history(self):
        with R8Mock(count=0) as first, R8Mock(count=1) as second:
            second.name = "Trabalho"
            second.notifications[0]["message_content"] = "Aviso @everyone"
            terminal = self.open(first)
            self.title(terminal, "geral | Comunidade")
            self.click(terminal, "[+ Adicionar]")
            self.wait(terminal, "URL do servidor")
            self.title(terminal, "Papo TUI")
            terminal.send(second.url + "\tjosé\tSynthetic!123\n")
            self.title(terminal, "geral | Trabalho (1)")
            self.click(terminal, "Comunidade")
            self.title(terminal, "geral | Comunidade (1)")
            history_reads = sum("/messages" in path for _, path, *_ in second.requests)
            second.emit(
                dict(
                    type="new_notification",
                    id=uuid(9000),
                    user_id=AUTHOR,
                    message_id=uuid(9001),
                    message_content="Mais uma @everyone",
                )
            )
            self.title(terminal, "geral | Comunidade (2)")
            self.assertEqual(
                sum("/messages" in path for _, path, *_ in second.requests), history_reads
            )


if __name__ == "__main__":
    test_main(
        1,
    )
