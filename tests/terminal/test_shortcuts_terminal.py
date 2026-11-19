"""Ctrl+S → Atalhos: list, change and restore command keys with synthetic servers."""

from test_context import main as test_main

import json
from pathlib import Path
import sys
import tempfile
import time
import unittest
from history_mock import HistoryMock
from terminal_support import Terminal, wait_for_screen

RIGHT, DOWN, END = "\x1bOC", "\x1bOB", "\x1bOF"
# The Super key is Command on macOS, which also gives some commands a Command
# key after their Ctrl default.
SUPER = "Cmd" if sys.platform == "darwin" else "Super"


class ShortcutsTerminal(unittest.TestCase):
    def screen(self, terminal):
        return "\n".join(terminal.screen.display)

    def wait(self, terminal, text, timeout=8):
        deadline = time.monotonic() + timeout
        while text not in self.screen(terminal) and time.monotonic() < deadline:
            terminal.read(0.03)
        self.assertIn(text, self.screen(terminal))

    def gone(self, terminal, text):
        wait_for_screen(terminal, lambda: text not in self.screen(terminal), f"{text!r} stayed")

    def row(self, terminal, text):
        return next((line for line in terminal.screen.display if text in line), "")

    def open(self, server):
        self.home = tempfile.TemporaryDirectory(prefix="papo-shortcuts-")
        self.addCleanup(self.home.cleanup)
        self.config = Path(self.home.name, "papo-tui", "config.json")
        terminal = Terminal(environment={"XDG_CONFIG_HOME": self.home.name})
        self.addCleanup(lambda: terminal.close() if terminal.process.poll() is None else None)
        terminal.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(terminal, "Mensagem 0250")
        return terminal

    def shortcuts(self, terminal, search):
        """Opens Ctrl+S → Atalhos, searches and selects the first result."""
        terminal.send("\x13")
        self.wait(terminal, "Configurações")
        terminal.send(RIGHT * 5)
        self.wait(terminal, "Sair do aplicativo")
        terminal.send(DOWN)
        self.wait(terminal, "Enter altera a função selecionada")
        terminal.send(search)
        self.wait(terminal, search)
        terminal.send(DOWN)

    def change(self, terminal, key):
        terminal.send("\r")
        self.wait(terminal, "Definir tecla")
        terminal.send("\r")
        self.wait(terminal, "Pressione a nova tecla")
        terminal.send(key)

    def save(self, terminal):
        terminal.send("\t\r")
        self.gone(terminal, "Configurações")

    def stored(self):
        return json.loads(self.config.read_text(encoding="utf-8")).get("shortcuts")

    def test_new_key_opens_the_command_and_is_saved(self):
        with HistoryMock() as server:
            t = self.open(server)
            self.shortcuts(t, "canal ou DM")
            self.wait(t, "Ctrl+K")
            self.change(t, "\x1bOR")
            wait_for_screen(
                t,
                lambda: "F3" in self.row(t, "Ir para um canal ou DM"),
                "The captured key was not shown",
            )
            self.assertIn("•", self.row(t, "Ir para um canal ou DM"))
            self.save(t)
            self.assertEqual(self.stored(), {"conversation.switcher": ["f3"]})
            t.send("\x1bOR")
            self.wait(t, "Ir para conversa")
            t.send("\x1b")
            self.gone(t, "Ir para conversa")
            # Ctrl+K is free again: global help right after it is not swallowed.
            t.send("\x0b\x1f")
            self.wait(t, "Ajuda global")
            t.send("F3")
            self.wait(t, "Ir para um canal ou DM")

    def test_super_key_is_captured_and_unused_ones_are_ignored(self):
        with HistoryMock() as server:
            t = self.open(server)
            # A Super combination no command uses neither closes the panel as Esc
            # nor reaches its field as text.
            t.send("\x0b")
            self.wait(t, "Ir para conversa")
            t.send("\x1b[106;9uzzq")
            self.wait(t, "zzq")
            self.assertNotIn("106;9u", self.screen(t))
            self.assertIn("Ir para conversa", self.screen(t))
            t.send("\x1b")
            self.gone(t, "Ir para conversa")
            self.shortcuts(t, "canal ou DM")
            # Kitty's keyboard protocol reports Super (Command on macOS) as bit 8.
            self.change(t, "\x1b[107;9u")
            wait_for_screen(
                t,
                lambda: f"{SUPER}+K" in self.row(t, "Ir para um canal ou DM"),
                "The Super key was not captured",
            )
            self.save(t)
            self.assertEqual(self.stored(), {"conversation.switcher": ["super+k"]})
            t.send("\x1b[107;9u")
            self.wait(t, "Ir para conversa")

    def test_key_of_another_command_moves_after_confirmation(self):
        with HistoryMock() as server:
            t = self.open(server)
            self.shortcuts(t, "canal ou DM")
            self.change(t, "\x02")
            self.wait(t, 'Ctrl+B já é usada por "Abrir mensagens fixadas"')
            t.send("\r")
            wait_for_screen(
                t,
                lambda: "Ctrl+B" in self.row(t, "Ir para um canal ou DM"),
                "The key did not move",
            )
            t.send("\x15fixadas")
            # Only Ctrl+B moves; on macOS pinned messages keep Command+B.
            kept = ["super+b"] if sys.platform == "darwin" else []
            self.wait(t, "Cmd+B" if kept else "sem tecla")
            self.save(t)
            self.assertEqual(
                self.stored(),
                {"conversation.switcher": ["ctrl+b"], "conversation.pinned": kept},
            )
            t.send("\x02")
            self.wait(t, "Ir para conversa")

    def test_refused_keys_cancel_and_restore(self):
        with HistoryMock() as server:
            t = self.open(server)
            self.shortcuts(t, "canal ou DM")
            # A bare letter would be typed into the message box.
            self.change(t, "k")
            self.wait(t, "Esta função age em campos de texto")
            # The capture continues; Esc as the kitty protocol reports it cancels.
            t.send("\x1b[27u")
            self.wait(t, "Enter altera a função selecionada")
            self.assertIn("Ctrl+K", self.row(t, "Ir para um canal ou DM"))
            self.assertIn("Configurações", self.screen(t))
            # Quitting can never be left without a key.
            t.send("\x15Sair do aplicativo")
            self.wait(t, "Ctrl+Q")
            t.send(DOWN + "\r")
            self.wait(t, "Remover as teclas")
            t.send(DOWN * 2 + "\r")
            self.wait(t, "Sair do aplicativo precisa de pelo menos uma tecla.")
            # Fixed keys are listed but cannot change.
            t.send("\x15Colar arquivos")
            self.wait(t, "Ctrl+V")
            t.send(DOWN + "\r")
            self.wait(t, "não podem ser alteradas")
            # Restore every default from the end of the unfiltered list.
            t.send("\x15canal ou DM")
            t.send(DOWN)
            self.change(t, "\x1bOR")
            wait_for_screen(t, lambda: "F3" in self.row(t, "Ir para um canal ou DM"), "No F3")
            t.send("\x15" + DOWN + END + "\r")
            self.wait(t, "Enter restaura as teclas padrão")
            t.send("\r")
            t.send("\x15canal ou DM")
            wait_for_screen(t, lambda: "Ctrl+K" in self.row(t, "Ir para um canal ou DM"), "Kept")
            # Esc closes without saving anything.
            t.send("\x1b")
            self.gone(t, "Configurações")
            self.assertFalse(self.config.exists() and self.stored())
            t.send("\x0b")
            self.wait(t, "Ir para conversa")


if __name__ == "__main__":
    test_main()
