"""ConPTY fixture lifecycle checks with real sockets and a controlled child."""

import importlib.util
from pathlib import Path
import socket
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch
import pyte


def load_backend():
    source = Path(__file__).parents[1] / "support/terminal_windows.py"
    spec = importlib.util.spec_from_file_location("tested_terminal_windows", source)
    module = importlib.util.module_from_spec(spec)
    # This tests teardown on every host without spawning a Windows process.
    with patch.dict(sys.modules, winpty=SimpleNamespace(Backend=None, PtyProcess=None)):
        spec.loader.exec_module(module)
    return module


backend = load_backend()


class Child:
    def __init__(self, alive, quit_works):
        self.alive = alive
        self.quit_works = quit_works
        self.closed = not alive
        self.fileobj, self.peer = socket.socketpair()
        self._server = socket.socket()
        self.writes = []

    def isalive(self):
        # Reproduce pywinpty's closed flag, which skips its socket teardown.
        self.closed = not self.alive
        return self.alive

    def write(self, value):
        self.writes.append(value)
        if self.quit_works:
            self.alive = False

    def close(self, force=False):
        if not self.closed:
            self.fileobj.close()
            self._server.close()
            if force:
                self.alive = False
            self.closed = True


class WindowsTerminalLifecycle(unittest.TestCase):
    def terminal(self, *, alive=True, quit_works=True):
        child = Child(alive, quit_works)
        self.addCleanup(child.peer.close)
        self.addCleanup(child.fileobj.close)
        self.addCleanup(child._server.close)
        terminal = backend.WindowsTerminal.__new__(backend.WindowsTerminal)
        terminal.child = child
        terminal._closed = False
        terminal._clean_exit = False
        terminal.read = Mock()

        def remove_configuration():
            self.assertFalse(child.alive, "Session lock must be released before cleanup")
            self.assertEqual(child.fileobj.fileno(), -1)
            self.assertEqual(child._server.fileno(), -1)

        terminal.configuration = SimpleNamespace(cleanup=Mock(side_effect=remove_configuration))
        return terminal

    def test_graceful_exit_closes_both_sockets_and_cleanup_is_idempotent(self):
        terminal = self.terminal()
        self.assertTrue(terminal.close())
        self.assertTrue(terminal.close())
        self.assertEqual(terminal.child.writes, ["\x11"])
        terminal.configuration.cleanup.assert_called_once()

    def test_already_exited_process_still_releases_its_sockets(self):
        terminal = self.terminal(alive=False)
        self.assertTrue(terminal.close())
        self.assertEqual(terminal.child.writes, [])
        terminal.configuration.cleanup.assert_called_once()

    def test_timeout_forces_exit_before_removing_configuration(self):
        terminal = self.terminal(quit_works=False)
        with patch.object(backend.time, "monotonic", side_effect=[0, 9]):
            self.assertFalse(terminal.close())
        self.assertFalse(terminal.close())
        terminal.configuration.cleanup.assert_called_once()

    def test_read_failure_still_releases_process_sockets_and_lock(self):
        terminal = self.terminal(quit_works=False)
        terminal.read.side_effect = OSError("controlled read failure")
        with (
            patch.object(backend.time, "monotonic", side_effect=[0, 1]),
            self.assertRaisesRegex(OSError, "controlled read failure"),
        ):
            terminal.close()
        terminal.configuration.cleanup.assert_called_once()


class WindowsTerminalReads(unittest.TestCase):
    def terminal(self):
        receiver, sender = socket.socketpair()
        self.addCleanup(receiver.close)
        self.addCleanup(sender.close)
        terminal = backend.WindowsTerminal.__new__(backend.WindowsTerminal)
        terminal.child = SimpleNamespace(
            fd=receiver.fileno(),
            fileobj=receiver,
            read=Mock(side_effect=AssertionError("Must not enter pywinpty's blocking decoder")),
        )
        terminal.output = bytearray()
        terminal.screen = pyte.Screen(40, 2)
        terminal.stream = pyte.ByteStream(terminal.screen)
        return terminal, sender

    def test_incomplete_utf8_returns_without_waiting_for_the_next_code_point(self):
        terminal, sender = self.terminal()
        sender.sendall(b"jos\xc3")
        terminal.read(0.03)
        self.assertEqual(terminal.screen.display[0].rstrip(), "jos")
        sender.sendall("é 😀".encode()[1:])
        terminal.read(0.03)
        self.assertEqual(terminal.screen.display[0].rstrip(), "josé 😀")
        self.assertEqual(terminal.output, "josé 😀".encode())

    def test_invalid_utf8_does_not_block_later_valid_output(self):
        terminal, sender = self.terminal()
        sender.sendall(b"\xffready")
        terminal.read(0.03)
        self.assertEqual(terminal.screen.display[0].rstrip(), "�ready")
        self.assertEqual(terminal.output, b"\xffready")

    def test_eof_ends_read_and_empty_read_sentinel_is_not_displayed(self):
        terminal, sender = self.terminal()
        sender.sendall(b"0011Ignore")
        terminal.read(0.03)
        self.assertEqual(terminal.output, b"")
        sender.close()
        terminal.read(0.03)
        self.assertEqual(terminal.output, b"")


if __name__ == "__main__":
    unittest.main()
