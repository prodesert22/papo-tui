import fcntl
import os
import pty
import select
import signal
import struct
import subprocess
import sys
import termios
import time
import unittest

import pyte

from mock_server import MockServer

BINARY = sys.argv.pop(1)


class ObservedScreen(pyte.Screen):
    def __init__(self, *args):
        self.visible_cursor_rows = []
        super().__init__(*args)

    def cursor_position(self, *args, **kwargs):
        super().cursor_position(*args, **kwargs)
        if not self.cursor.hidden:
            self.visible_cursor_rows.append(self.cursor.y)


class Terminal:
    def __init__(self):
        self.master, self.slave = pty.openpty()
        self.original = termios.tcgetattr(self.slave)
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
        env = dict(os.environ, TERM="xterm-256color", LANG="C.UTF-8", LC_ALL="C.UTF-8")
        self.process = subprocess.Popen([BINARY], stdin=self.slave, stdout=self.slave, stderr=self.slave,
                                        env=env, start_new_session=True)
        self.output = bytearray()
        self.screen = ObservedScreen(80, 24)
        self.stream = pyte.ByteStream(self.screen)
        self.read(0.15)

    def read(self, seconds=0.1):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            ready, _, _ = select.select([self.master], [], [], 0.02)
            if ready:
                data = os.read(self.master, 65536)
                self.output.extend(data)
                self.stream.feed(data)

    def send(self, value):
        os.write(self.master, value.encode())
        self.read()

    def resize(self, rows, cols):
        self.screen.resize(lines=rows, columns=cols)
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        self.process.send_signal(signal.SIGWINCH)
        self.read()

    def wait_text(self, text):
        deadline = time.monotonic() + 5
        while text.encode() not in self.output and time.monotonic() < deadline:
            self.read()
        if text.encode() not in self.output:
            raise AssertionError(f"Texto esperado ausente: {text}; fim da saída={bytes(self.output[-1000:])!r}")

    def click(self, row, column):
        # SGR mouse reports, parsed by ncurses rather than by application code.
        self.send(f"\x1b[<0;{column + 1};{row + 1}M\x1b[<0;{column + 1};{row + 1}m")

    def close(self, interrupt=False):
        if self.process.poll() is None:
            if interrupt:
                self.process.send_signal(signal.SIGTERM)
            else:
                self.send("\x11")
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
                raise
        self.read()
        restored = termios.tcgetattr(self.slave) == self.original
        os.close(self.master)
        os.close(self.slave)
        return restored


class TerminalTests(unittest.TestCase):
    def test_four_fields_resize_password_mask_and_real_request(self):
        with MockServer("private") as server:
            terminal = Terminal()
            try:
                terminal.wait_text("URL do servidor")
                terminal.send(server.url + "\tjosé\tSynthetic!123\tServer!123")
                terminal.resize(16, 40)
                terminal.resize(30, 100)
                terminal.send("\t\n")
                terminal.wait_text("Login realizado")
                self.assertEqual(server.requests[2][3], {"username": "josé", "password": "Synthetic!123"})
                self.assertNotIn(b"Synthetic!123", terminal.output)
                self.assertNotIn(b"Server!123", terminal.output)
                terminal.send("\n")
                terminal.wait_text("Você saiu da conta")
            finally:
                restored = terminal.close()
            self.assertTrue(restored)
            self.assertEqual(terminal.process.returncode, 0)

    def test_paste_newline_does_not_submit(self):
        with MockServer() as server:
            terminal = Terminal()
            try:
                terminal.send(server.url + "\tjosé\t")
                terminal.send("\x1b[200~Synthetic!123\n\x1b[201~")
                self.assertEqual(server.requests, [])
                terminal.send("\n")
                terminal.wait_text("Login realizado")
                self.assertNotIn(b"Synthetic!123", terminal.output)
            finally:
                restored = terminal.close(interrupt=True)
            self.assertTrue(restored)

    def test_slow_request_can_be_cancelled_and_terminal_exited(self):
        with MockServer("slow") as server:
            terminal = Terminal()
            try:
                terminal.send(server.url + "\tjosé\tSynthetic!123\n")
                terminal.send("\x1b")
                # curses can retain the 'Con' prefix from 'Consultando'.
                terminal.wait_text("cancelada")
                self.assertNotIn(b"Synthetic!123", terminal.output)
            finally:
                restored = terminal.close()
            self.assertTrue(restored)

    def test_password_spaces_and_editing_are_preserved(self):
        with MockServer() as server:
            terminal = Terminal()
            try:
                terminal.send(server.url + "\tjosé\t Synthetic!123X\x7f \n")
                terminal.wait_text("inválidos")
                self.assertEqual(server.requests[1][3]["password"], " Synthetic!123 ")
                self.assertNotIn(b"Synthetic!123", terminal.output)
            finally:
                restored = terminal.close()
            self.assertTrue(restored)

    def test_repeated_backspace_stays_in_each_field(self):
        with MockServer("private") as server:
            terminal = Terminal()
            try:
                for index, value in enumerate([server.url, "josé", "Synthetic!123", "Server!123"]):
                    if index:
                        terminal.send("\t")
                    # Repeat past the beginning of both populated and empty inputs.
                    terminal.screen.visible_cursor_rows.clear()
                    terminal.send("apagar" + "\x7f" * 40 + "\x08" * 10 + value)
                    self.assertEqual(terminal.screen.cursor.y, 6 + 3 * index)
                    self.assertTrue(all(row == 6 + 3 * index for row in terminal.screen.visible_cursor_rows))
                terminal.send("\n")
                terminal.wait_text("Login realizado")
                self.assertEqual(server.requests[1][3], {"server_password": "Server!123"})
                self.assertEqual(server.requests[2][3], {"username": "josé", "password": "Synthetic!123"})
                self.assertNotIn(b"Synthetic!123", terminal.output)
                self.assertNotIn(b"Server!123", terminal.output)
            finally:
                restored = terminal.close()
            self.assertTrue(restored)

    def test_backspace_at_start_of_populated_field_does_not_move_cursor(self):
        with MockServer() as server:
            terminal = Terminal()
            try:
                terminal.send(server.url + "\tjosé\x1bOH")
                for _ in range(4):
                    terminal.send("\x7f" * 16)
                    self.assertEqual((terminal.screen.cursor.y, terminal.screen.cursor.x), (9, 12))
                terminal.send("\tSynthetic!123\n")
                terminal.wait_text("Login realizado")
                self.assertEqual(server.requests[1][3], {"username": "josé", "password": "Synthetic!123"})
            finally:
                restored = terminal.close()
            self.assertTrue(restored)

    def test_mouse_focus_fields_login_and_logout_after_resize(self):
        with MockServer("private") as server:
            terminal = Terminal()
            try:
                terminal.resize(30, 100)
                # The centered card now begins at row 4 and column 18.
                for row, value in [(9, server.url), (12, "josé"), (15, "Synthetic!123"), (18, "Server!123")]:
                    terminal.click(row, 22)
                    terminal.send(value)
                    self.assertEqual(terminal.screen.cursor.y, row)
                terminal.click(21, 24)
                terminal.wait_text("Login realizado")
                self.assertEqual(len(server.requests), 4)
                terminal.click(21, 24)
                terminal.wait_text("Você saiu da conta")
                self.assertEqual(len(server.requests), 5)
                self.assertNotIn(b"Synthetic!123", terminal.output)
                self.assertNotIn(b"Server!123", terminal.output)
            finally:
                restored = terminal.close()
            self.assertTrue(restored)

    def test_mouse_places_caret_and_ignores_outside_clicks(self):
        with MockServer() as server:
            terminal = Terminal()
            try:
                terminal.send(server.url + "\tjosé")
                terminal.click(9, 14)
                self.assertEqual((terminal.screen.cursor.y, terminal.screen.cursor.x), (9, 14))
                terminal.send("X")
                terminal.click(0, 0)
                self.assertEqual((terminal.screen.cursor.y, terminal.screen.cursor.x), (9, 15))
                terminal.send("\x7f\tSynthetic!123")
                terminal.click(18, 14)
                terminal.wait_text("Login realizado")
                self.assertEqual(server.requests[1][3]["username"], "josé")
            finally:
                restored = terminal.close()
            self.assertTrue(restored)


if __name__ == "__main__":
    unittest.main()
