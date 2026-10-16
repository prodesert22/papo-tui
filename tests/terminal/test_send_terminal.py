"""R6 editor adapter on a real PTY; the product route is owned by R5."""

import time
import unittest
from mock_server import MockServer
from test_terminal import Terminal


class SendTerminal(unittest.TestCase):
    def screen(self, terminal):
        return "\n".join(terminal.screen.display)

    def wait(self, terminal, text):
        deadline = time.monotonic() + 5
        while text not in self.screen(terminal) and time.monotonic() < deadline:
            terminal.read(0.03)
        self.assertIn(text, self.screen(terminal))

    def test_editor_stays_responsive_and_keeps_append_after_confirmation(self):
        with MockServer("send-delayed") as server:
            terminal = Terminal((server.url,))
            try:
                self.wait(terminal, "Canal A")
                terminal.send("Olá, ação!\n")
                terminal.send(" continuação")
                self.wait(terminal, "Mensagem confirmada")
                self.assertEqual(server.message_count, 1)
                self.assertEqual(server.sent_messages[0]["content"], "Olá, ação!")
                self.assertIn("continuação", terminal.screen.display[9])
                self.assertNotIn("Olá", terminal.screen.display[9])
                terminal.resize(22, 44)
                self.assertIn("continuação", terminal.screen.display[9])
                self.assertNotIn(b"Synthetic!123", terminal.output)
            finally:
                self.assertTrue(terminal.close())

    def test_switch_channel_and_return_preserves_draft_and_cursor(self):
        with MockServer() as server:
            terminal = Terminal((server.url,))
            try:
                self.wait(terminal, "Canal A")
                terminal.send("Primeiro\x1bOD\x1bOD\t")
                self.wait(terminal, "Canal B")
                terminal.send("Segundo\t")
                self.wait(terminal, "Canal A")
                self.assertIn("Primeiro", terminal.screen.display[9])
                terminal.send("X")
                self.assertIn("PrimeiXro", terminal.screen.display[9])
                self.assertEqual(server.message_count, 0)
            finally:
                self.assertTrue(terminal.close())

    def test_rejection_keeps_editor_and_can_exit_while_sending(self):
        with MockServer("send-forbidden") as server:
            terminal = Terminal((server.url,))
            try:
                self.wait(terminal, "Canal A")
                terminal.send("Texto que deve ficar\n")
                self.wait(terminal, "Sem permissão")
                self.assertIn("Texto que deve ficar", terminal.screen.display[9])
                self.assertEqual(server.message_count, 1)
            finally:
                self.assertTrue(terminal.close())
        with MockServer("send-delayed") as server:
            terminal = Terminal((server.url,))
            try:
                self.wait(terminal, "Canal A")
                terminal.send("Envio em andamento\n")
                started = time.monotonic()
            finally:
                self.assertTrue(terminal.close())
            self.assertLess(time.monotonic() - started, 0.4)

    def test_uncertain_retry_requires_explicit_choice(self):
        with MockServer("send-drop-once") as server:
            terminal = Terminal((server.url,))
            try:
                self.wait(terminal, "Canal A")
                terminal.send("Mensagem incerta\n")
                self.wait(terminal, "Envio sem confirmação")
                self.assertEqual(server.message_count, 1)
                terminal.send("\n")
                self.wait(terminal, "Reenviar pode duplicar")
                self.assertEqual(server.message_count, 1)
                terminal.send("\x1b")
                self.assertEqual(server.message_count, 1)
                terminal.send("\n\n")
                self.wait(terminal, "Mensagem confirmada")
                self.assertEqual(server.message_count, 2)
            finally:
                self.assertTrue(terminal.close())

    def test_paste_preserves_lines_and_does_not_submit(self):
        with MockServer() as server:
            terminal = Terminal((server.url,))
            try:
                self.wait(terminal, "Canal A")
                terminal.send("\x1b[200~Ação\r\nsegunda linha\x11\x1b[201~")
                self.assertEqual(server.message_count, 0)
                terminal.send("\n")
                self.wait(terminal, "Mensagem confirmada")
                self.assertEqual(server.sent_messages[0]["content"], "Ação\nsegunda linha")
            finally:
                self.assertTrue(terminal.close())


if __name__ == "__main__":
    unittest.main()
