"""Interface language through the real TUI: English screens and switching in Settings."""

from test_context import main as test_main

import base64
import json
from pathlib import Path
import re
import tempfile
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
            # The file now exists; System, three tabs back, says it is in use.
            t.send("\x1bOD" * 3)
            self.wait(t, "Configuration file:")
            self.wait(t, str(config))
            self.wait(t, "In use. Changes made on this computer are saved in this file.")
            self.wait(t, "Copy path")

    def settings(self, server, root=None):
        environment = {"PAPO_LANGUAGE": "pt"}
        if root:
            environment["XDG_CONFIG_HOME"] = root
        t = self.open(server, environment)
        t.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(t, "[Canais]")
        self.wait(t, "Mensagem 0250")
        t.send("\x13")
        self.wait(t, "Configurações")
        return t

    def seeded(self, content):
        root = tempfile.TemporaryDirectory(prefix="pl-")
        self.addCleanup(root.cleanup)
        config = Path(root.name, "papo-tui", "config.json")
        config.parent.mkdir(mode=0o700)
        config.write_text(content, encoding="utf-8")
        config.chmod(0o600)
        return root.name, config

    def refuse_english(self, t):
        """Chooses English in Aparência and asks to save it."""
        t.send("\x1bOC\t" + "\x1bOB" * 3)
        self.wait(t, "Idioma: < Sistema >")
        t.send("\x1bOC\x1bOC")
        self.wait(t, "Idioma: < English >")
        t.send("\t\n")

    def system_tab(self, t):
        """From the buttons of Aparência: back to the tabs, then five tabs ahead."""
        t.send("\x1b[Z\x1b[Z" + "\x1bOC" * 5)
        self.wait(t, "Arquivo de configuração:")

    def test_sistema_shows_where_the_configuration_file_is_saved_and_copies_it(self):
        with HistoryMock() as server:
            t = self.settings(server)
            config = Path(t.configuration.name, "papo-tui", "config.json")
            self.assertNotIn("Arquivo de configuração:", self.screen(t))
            # Sistema comes before Conta and Sobre, the last tabs: three steps back.
            t.send("\x1bOD" * 3)
            self.wait(t, "Arquivo de configuração:")
            self.wait(t, str(config))
            # Signing in saves nothing on this computer.
            self.wait(t, "Ainda não existe. Será criado na primeira alteração salva.")
            self.assertFalse(config.exists())
            self.wait(t, "Senhas e cookies de sessão não são gravados neste arquivo.")
            self.wait(t, "Copiar caminho")
            self.assertNotIn("Salvar e aplicar", self.screen(t))
            # The tab has nothing to edit: Tab goes straight to its button.
            copies = len(re.findall(rb"\x1b\]52;c;", t.output))
            t.send("\t\n")
            self.wait(t, "Cópia solicitada.")
            copied = re.findall(rb"\x1b\]52;c;([A-Za-z0-9+/=]*)\x07", t.output)
            self.assertEqual(len(copied), copies + 1)
            self.assertEqual(base64.b64decode(copied[-1]).decode("utf-8"), str(config))
            # A narrow terminal wraps the path instead of cutting it.
            t.resize(22, 44)
            self.wait(t, "Arquivo de configuração:")
            self.wait(t, "config.json")
            self.assertNotIn(str(config), self.screen(t))
            inside = [re.sub(r"^x.?x\s*|\s*x.?x$", "", line) for line in t.screen.display]
            self.assertIn(str(config), "".join(inside), self.screen(t))
            # What the file never holds comes first, so it survives a short window.
            self.wait(t, "gravados neste arquivo.")
            self.wait(t, "Copiar caminho")
            t.resize(24, 80)
            self.wait(t, str(config))
            # Leaving the tab drops the notice.
            t.send("\x1b[Z\x1bOD")
            self.gone(t, "Cópia solicitada.")
            self.assertNotIn("Arquivo de configuração:", self.screen(t))

    def test_a_file_from_a_newer_version_keeps_the_language_and_says_so(self):
        # A later format, as another build of the application leaves it.
        newer = json.dumps({"version": 9000, "profiles": [], "selected_profile": None})
        root, config = self.seeded(newer)
        with HistoryMock() as server:
            t = self.settings(server, root)
            self.refuse_english(t)
            # Refused: the window stays open, in Portuguese, and says why.
            self.wait(t, "Configuração bloqueada: arquivo de uma versão mais nova do Papo TUI.")
            self.assertIn("Aparência", self.screen(t))
            self.assertNotIn("Appearance", self.screen(t))
            self.system_tab(t)
            self.wait(t, str(config))
            self.wait(t, "Bloqueado: foi gravado por uma versão mais nova do Papo TUI")
            # Wrapped between words: the formats are on the next row.
            self.wait(t, "9000; esta lê até o 14).")
            self.assertNotIn("corrompido", self.screen(t))
            self.assertEqual(config.read_text("utf-8"), newer)

    def test_a_damaged_file_keeps_the_language_and_is_not_called_a_newer_version(self):
        root, config = self.seeded("{broken")
        with HistoryMock() as server:
            t = self.settings(server, root)
            self.refuse_english(t)
            self.wait(t, "Configuração bloqueada; corrija o arquivo")
            self.assertNotIn("mais nova", self.screen(t))
            self.system_tab(t)
            self.wait(t, str(config))
            self.wait(t, "Bloqueado: o arquivo está corrompido ou não é uma configuração")
            self.assertNotIn("mais nova", self.screen(t))
            self.assertEqual(config.read_text("utf-8"), "{broken")


if __name__ == "__main__":
    test_main()
