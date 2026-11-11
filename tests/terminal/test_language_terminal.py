"""Interface language through the real TUI: English screens and switching in Settings."""

from test_context import main as test_main

import json
from pathlib import Path
import time
import unittest
from history_mock import HistoryMock
from terminal_support import Terminal, wait_for_screen


class LanguageTerminal(unittest.TestCase):
    def screen(self, terminal):
        return "\n".join(terminal.screen.display)

    def wait(self, terminal, text):
        wait_for_screen(terminal, lambda: text in self.screen(terminal), f"Missing {text!r}", 8)

    def gone(self, terminal, text):
        wait_for_screen(terminal, lambda: text not in self.screen(terminal), f"{text!r} stayed", 8)

    def open(self, server, environment):
        terminal = Terminal(environment=environment)
        self.addCleanup(lambda: terminal.close() if terminal.process.poll() is None else None)
        return terminal

    def test_english_screens_when_the_system_language_is_english(self):
        with HistoryMock() as server:
            t = self.open(server, {"PAPO_LANGUAGE": "en"})
            self.wait(t, "Sign in to your community")
            self.wait(t, "Server URL")
            self.assertNotIn("URL do servidor", self.screen(t))
            t.send(server.url + "\tjosé\tSynthetic!123\n")
            self.wait(t, "[Channels]")
            self.wait(t, "Message #geral")
            # Remote names and messages are never translated.
            self.wait(t, "Mensagem 0250")
            self.wait(t, "# geral · History")
            t.send("\x07")
            self.wait(t, "Contextual help")
            self.wait(t, "Key combination")
            t.send("\x1b")
            self.gone(t, "Contextual help")
            t.send("\x0b")
            self.wait(t, "Go to conversation")
            self.wait(t, "channel, DM or server name")
            t.send("\x1b")
            self.gone(t, "Go to conversation")
            for portuguese in ("Canais", "Histórico", "Conversar em", "Ajuda do contexto"):
                self.assertNotIn(portuguese, self.screen(t))

    def test_choosing_english_in_settings_applies_at_once_and_is_saved(self):
        with HistoryMock() as server:
            t = self.open(server, {"PAPO_LANGUAGE": "pt"})
            config = Path(t.configuration.name, "papo-tui", "config.json")
            t.send(server.url + "\tjosé\tSynthetic!123\n")
            self.wait(t, "[Canais]")
            self.wait(t, "Mensagem 0250")
            t.send("\x13")
            self.wait(t, "Configurações")
            # Aparência, then from the Normal mode down to Idioma.
            t.send("\x1bOC\t" + "\x1bOB" * 3)
            self.wait(t, "Idioma: < Sistema >")
            t.send("\x1bOC\x1bOC")
            self.wait(t, "Idioma: < English >")
            # Salvar e aplicar.
            t.send("\t\n")
            self.gone(t, "Idioma: <")
            self.wait(t, "[Channels]")
            self.wait(t, "Message #geral")
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if (
                    config.exists()
                    and json.loads(config.read_text("utf-8")).get("language") == "en"
                ):
                    break
                t.read(0.05)
            self.assertEqual(json.loads(config.read_text("utf-8"))["language"], "en")
            t.send("\x13")
            self.wait(t, "Settings")
            self.wait(t, "Appearance")


if __name__ == "__main__":
    test_main()
