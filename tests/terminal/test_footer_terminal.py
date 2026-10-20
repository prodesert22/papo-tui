"""Contextual footer through the real TUI with synthetic HTTP/WebSocket peers."""

from test_context import main as test_main

import time
import unittest
from history_mock import CHANNEL
from realtime_mock import RealtimeMock
from terminal_support import Terminal, wait_for_screen


class FooterTerminal(unittest.TestCase):
    def resize(self, terminal, rows, columns):
        terminal.resize(rows, columns)
        wait_for_screen(
            terminal,
            lambda: terminal.screen.buffer[rows - 1][columns - 1].data in ("j", "┘"),
            "Resized footer frame did not arrive",
        )

    def wait(self, terminal, text):
        deadline = time.monotonic() + 8
        while text not in "\n".join(terminal.screen.display) and time.monotonic() < deadline:
            terminal.read(0.03)
        self.assertIn(text, "\n".join(terminal.screen.display))

    def footer(self, terminal):
        line = terminal.screen.display[-2]
        self.assertIn("F3", line)
        self.assertIn("F8", line)
        return line

    def wait_footer(self, terminal, text):
        wait_for_screen(
            terminal,
            lambda: text in terminal.screen.display[-2],
            f"Missing footer text: {text}",
        )
        return self.footer(terminal)

    def open(self, server):
        terminal = Terminal()
        terminal.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(terminal, "geral")
        return terminal

    def test_hints_follow_focus_and_notices_share_only_the_last_line(self):
        with RealtimeMock(count=0) as server:
            terminal = self.open(server)
            try:
                self.resize(terminal, 24, 220)
                self.wait_footer(terminal, "←→ servidores")
                self.wait_footer(terminal, "F3 busca")
                self.wait_footer(terminal, "F8 fixadas")
                self.wait_footer(terminal, "F9 notificações")
                terminal.send("\x1bOB")
                self.wait(terminal, "Foco: Canais")
                self.wait_footer(terminal, "↑↓ canais")
                self.wait_footer(terminal, "F9 notificações")
                self.assertNotIn("F7", self.footer(terminal))
                self.assertNotIn("Enter enviar", self.footer(terminal))
                terminal.send("\n")
                self.wait(terminal, "Nenhuma mensagem neste canal")
                self.wait_footer(terminal, "↑↓ rolar")
                self.wait_footer(terminal, "F7 mensagens")
                self.wait_footer(terminal, "F9 notificações")
                self.assertNotIn("↑↓ canais", self.footer(terminal))
                terminal.send("Mensagem pelo rodapé\n")
                self.wait(terminal, "Mensagem confirmada")
                line = self.footer(terminal)
                self.assertIn("Enter enviar", line)
                self.assertNotIn("F7", line)
                self.assertIn("Tempo real conectado", line)
                self.assertIn("Mensagem confirmada pelo servidor", line)
                server.emit(dict(type="future_event", channel_id=CHANNEL))
                self.wait(terminal, "não suportado ignorado")
                line = self.footer(terminal)
                self.assertIn("Tempo real conectado", line)
                self.assertIn("Mensagem confirmada pelo servidor", line)
                self.assertIn("Evento de tempo real não suportado ignorado", line)
                self.assertEqual(line.count(" | "), 2)
                self.assertLess(
                    line.index("Tempo real conectado"), line.index("Mensagem confirmada")
                )
                self.assertLess(
                    line.index("Mensagem confirmada"), line.index("Evento de tempo real")
                )
                self.assertNotIn("Mensagem confirmada", "\n".join(terminal.screen.display[:-2]))
                self.assertNotIn("Evento de tempo real", "\n".join(terminal.screen.display[:-2]))
                self.resize(terminal, 24, 80)
                line = self.footer(terminal)
                self.assertIn("Enter", line)
                self.assertIn("+1", line)  # The longer hint collapses the oldest notice.
                self.assertIn("F9 notificações", line)
                self.assertIn("Mensagem confirmada", line)
                self.assertIn("Evento ignorado", line)
                self.assertEqual(line.count(" | "), 2)
                self.resize(terminal, 24, 220)
                server.emit(dict(type="error", channel_id=CHANNEL))
                self.wait(terminal, "O servidor informou um erro")
                line = self.footer(terminal)
                self.assertNotIn("não suportado", line)
                self.assertIn("Mensagem confirmada", line)
                self.assertIn("Tempo real conectado", line)
                self.assertEqual(line.count(" | "), 2)
                self.resize(terminal, 14, 44)
                self.wait_footer(terminal, "Enter")
                self.wait_footer(terminal, "F9")
                self.assertEqual(len(self.footer(terminal)), 44)
                self.resize(terminal, 24, 220)
                self.wait_footer(terminal, "O servidor informou um erro")
                self.wait_footer(terminal, "Mensagem confirmada pelo servidor")
                terminal.send("\x1b\x1bOD\x1bOH\x1bOA\x1bOA")
                self.wait(terminal, "Foco: Abas")
                self.wait_footer(terminal, "←→ servidores")
                self.assertNotIn("F7", self.footer(terminal))
            finally:
                self.assertTrue(terminal.close())

    def test_unread_indicator_is_clickable_in_the_notification_area(self):
        with RealtimeMock(count=30) as server:
            terminal = self.open(server)
            try:
                self.resize(terminal, 24, 160)
                terminal.send("\x1bOB\n")
                self.wait(terminal, "Mensagem 0030")
                terminal.send("\x1bOH")
                message = dict(server.message(31), content="Mensagem nova fora da leitura")
                server.emit(dict(message, type="message"))
                self.wait(terminal, "1 novas mensagens")
                line = self.footer(terminal)
                self.assertIn("1 novas mensagens", line)
                terminal.click(terminal.screen.lines - 2, line.index("1 novas mensagens") + 1)
                self.wait(terminal, message["content"])
                self.assertNotIn("novas mensagens", self.footer(terminal))
            finally:
                self.assertTrue(terminal.close())


if __name__ == "__main__":
    test_main(
        1,
    )
