"""Windows end-to-end scenarios use the same observed TUI and synthetic HTTP."""

from test_context import main as test_main

import time
import ctypes
import json
from pathlib import Path
import tempfile
import unittest
from history_mock import HistoryMock
from terminal_support import Terminal, TerminalStreamTests  # noqa: F401 - shared observer regression tests


class WindowsTerminalTests(unittest.TestCase):
    def wait(self, terminal, text):
        deadline = time.monotonic() + 10
        while (
            text not in "\n".join(terminal.screen.display)
            and terminal.process.poll() is None
            and time.monotonic() < deadline
        ):
            terminal.read(0.03)
        self.assertIn(
            text, "\n".join(terminal.screen.display), f"Process exit: {terminal.process.poll()}"
        )

    def login(self, terminal, server):
        terminal.send(server.url)
        terminal.send("\t")
        terminal.send("josé")
        self.wait(terminal, "josé")
        terminal.send("\t")
        terminal.send("Synthetic!123")
        terminal.send("\r")
        self.wait(terminal, "geral")

    def test_login_resize_unicode_send_and_exit(self):
        with HistoryMock() as server:
            terminal = Terminal()
            try:
                self.login(terminal, server)
                terminal.send("\x1bOB\r")
                self.wait(terminal, "Mensagem 0250")
                terminal.resize(28, 100)
                deadline = time.monotonic() + 8
                while terminal.screen.buffer[27][99].data != "┘" and time.monotonic() < deadline:
                    terminal.read(0.03)
                self.assertEqual(terminal.screen.buffer[27][99].data, "┘")
                terminal.send("Olá 😀\r")
                deadline = time.monotonic() + 10
                while not server.sent_messages and time.monotonic() < deadline:
                    terminal.read(0.03)
                self.assertTrue(server.sent_messages)
                self.assertEqual(server.sent_messages[-1]["content"], "Olá 😀")
                self.assertNotIn(b"Synthetic!123", terminal.output)
                self.assertIsNone(terminal.process.poll())
            finally:
                self.assertTrue(terminal.close())
            self.assertEqual(terminal.process.poll(), 0)

    def test_native_credentials_restore_logout_and_instance_scope(self):
        # Only a newly generated temporary config can select this target.
        # Cleanup deletes precisely that synthetic credential, never enumerates.
        with (
            tempfile.TemporaryDirectory(prefix="papo-cofre-á-") as temporary,
            HistoryMock() as server,
        ):
            root = Path(temporary)
            env = {"XDG_CONFIG_HOME": str(root / "config"), "XDG_STATE_HOME": str(root / "state")}
            server.cookie_max_age = 30 * 86400
            terminals = []
            target = None
            try:
                first = Terminal(environment=env)
                terminals.append(first)
                self.login(first, server)
                deadline = time.monotonic() + 10
                marker = None
                while time.monotonic() < deadline:
                    paths = list((root / "state/papo-tui").glob("session-*.json"))
                    if paths:
                        marker = paths[0]
                        state = json.loads(marker.read_text(encoding="utf-8"))
                        if state.get("restore_allowed"):
                            break
                    first.read(0.03)
                self.assertIsNotNone(marker)
                target = "PapoTUI/" + marker.stem.removeprefix("session-")
                self.assertTrue(json.loads(marker.read_text(encoding="utf-8"))["restore_allowed"])
                self.assertTrue(first.close())
                second = Terminal(environment=env)
                terminals.append(second)
                self.wait(second, "geral")
                logins = [
                    r for r in server.requests if r[0] == "POST" and r[1].endswith("/auth/login")
                ]
                self.assertEqual(len(logins), 1)
                second.send("\x0c")
                self.wait(second, "Você saiu da conta")
                self.assertTrue(second.close())
                self.assertFalse(json.loads(marker.read_text(encoding="utf-8"))["restore_allowed"])
                third = Terminal(environment=env)
                terminals.append(third)
                self.wait(third, "URL do servidor")
                self.assertNotIn("geral", "\n".join(third.screen.display))
                for path in root.rglob("*.json"):
                    self.assertNotIn("Synthetic!123", path.read_text(encoding="utf-8"))
                    self.assertNotIn("user-fixture", path.read_text(encoding="utf-8"))
            finally:
                for terminal in terminals:
                    if terminal.process.poll() is None:
                        terminal.close()
                if target:
                    api = ctypes.WinDLL("Advapi32.dll", use_last_error=True)
                    api.CredDeleteW.argtypes = [ctypes.c_wchar_p, ctypes.c_ulong, ctypes.c_ulong]
                    api.CredDeleteW(target, 1, 0)

    def test_cancel_pending_login_preserves_responsiveness(self):
        with HistoryMock("slow") as server:
            terminal = Terminal()
            try:
                terminal.send(server.url + "\tjosé\tSynthetic!123\r\x1b")
                terminal.resize(24, 80)
            finally:
                self.assertTrue(terminal.close())


if __name__ == "__main__":
    test_main(
        1,
    )
