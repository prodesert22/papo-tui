"""Synthetic end-to-end smoke test of an installed distribution entrypoint."""

import time
import unittest
from history_mock import HistoryMock
from test_terminal import Terminal


class InstalledApplication(unittest.TestCase):
    def wait(self, terminal, text):
        end = time.monotonic() + 10
        while text not in "\n".join(terminal.screen.display) and time.monotonic() < end:
            terminal.read(0.03)
        self.assertIn(text, "\n".join(terminal.screen.display))

    def test_installed_login_conversation_and_unicode_send(self):
        with HistoryMock() as server:
            terminal = Terminal()
            try:
                terminal.send(server.url + "\tjosé\tSynthetic!123\n")
                self.wait(terminal, "geral")
                terminal.send("\x1bOB\n")
                self.wait(terminal, "Mensagem 0250")
                terminal.send("Instalação 😀\n")
                end = time.monotonic() + 10
                while not server.sent_messages and time.monotonic() < end:
                    terminal.read(0.03)
                self.assertTrue(server.sent_messages)
                self.assertEqual(server.sent_messages[-1]["content"], "Instalação 😀")
                self.assertNotIn(b"Synthetic!123", terminal.output)
            finally:
                self.assertTrue(terminal.close())


if __name__ == "__main__":
    unittest.main()
