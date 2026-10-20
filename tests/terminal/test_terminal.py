"""Terminal transport and login regression tests."""

from test_context import main as test_main
import os
import resource
import signal
import time
import termios
from wcwidth import wcswidth
import unittest
from mock_server import MockServer
from terminal_support import Terminal, wait_for_screen


class TerminalTests(unittest.TestCase):
    def test_kitty_colors_do_not_redefine_the_reloadable_indexed_palette(self):
        for terminal_type in ("xterm-kitty", "xterm-256color"):
            with self.subTest(terminal_type=terminal_type):
                terminal = Terminal(
                    terminal_type=terminal_type,
                    environment={"KITTY_WINDOW_ID": "papo-synthetic-window"},
                )
                try:
                    self.assertIn("URL do servidor", "\n".join(terminal.screen.display))
                    self.assertNotIn(b"\x1b]4;", terminal.output)
                    ordinary = terminal.screen.buffer[2][12]
                    terminal.send("\tcor estável\x1bOH\x1b[1;2F")
                    terminal.resize(30, 100)
                    terminal.resize(24, 80)
                    self.assertIn("cor estável", "\n".join(terminal.screen.display))
                    self.assertNotIn(b"\x1b]4;", terminal.output)
                    after = terminal.screen.buffer[2][12]
                    self.assertEqual((ordinary.fg, ordinary.bg), (after.fg, after.bg))
                finally:
                    self.assertTrue(terminal.close())

    def test_three_areas_and_wide_text_survive_resize(self):
        terminal = Terminal()
        try:
            wait_for_screen(
                terminal,
                lambda: "[ Entrar ]" in terminal.screen.display[18],
                "Initial login frame did not arrive",
            )
            self.assertIn("papo", terminal.screen.display[2])
            self.assertIn("Usuário (username)", terminal.screen.display[8])
            self.assertIn("[ Entrar ]", terminal.screen.display[18])
            for row in (4, 17):
                # pyte leaves DEC line drawing as 'q' in UTF-8 mode.
                rule = terminal.screen.display[row][10:70]
                self.assertIn(rule, ("q" * 60, "─" * 60))
            terminal.send("\tação界語")
            self.assertEqual(terminal.wait_cursor(9, 20), (9, 20))
            self.assertIn("ação界語", terminal.screen.display[9])
            self.assertEqual((terminal.screen.cursor.y, terminal.screen.cursor.x), (9, 20))
            terminal.resize(16, 40)
            wait_for_screen(
                terminal,
                lambda: "Amplie o terminal" in terminal.screen.display[0],
                "Compact login frame did not arrive",
            )
            self.assertIn("Amplie o terminal", terminal.screen.display[0])
            terminal.resize(2, 18)
            wait_for_screen(
                terminal,
                lambda: terminal.screen.display[1].rstrip() == "Ctrl+Q: sair",
                "Minimal login frame did not arrive",
            )
            self.assertEqual(terminal.screen.display[1].rstrip(), "Ctrl+Q: sair")
            terminal.resize(22, 44)
            self.assertEqual(terminal.wait_cursor(8, 14), (8, 14))
            self.assertIn("ação界語", terminal.screen.display[8])
            self.assertEqual((terminal.screen.cursor.y, terminal.screen.cursor.x), (8, 14))
            terminal.resize(30, 100)
            self.assertEqual(terminal.wait_cursor(12, 30), (12, 30))
            self.assertIn("ação界語", terminal.screen.display[12])
            self.assertEqual((terminal.screen.cursor.y, terminal.screen.cursor.x), (12, 30))
            terminal.send("\x7f")
            self.assertEqual(terminal.wait_cursor(12, 28), (12, 28))
            self.assertIn("ação界", terminal.screen.display[12])
            self.assertNotIn("語", terminal.screen.display[12])
            self.assertEqual((terminal.screen.cursor.y, terminal.screen.cursor.x), (12, 28))
        finally:
            restored = terminal.close()
        self.assertTrue(restored)

    def test_exit_keys_and_signals_restore_terminal(self):
        for name, exit_key, signal_number in (
            ("Ctrl+Q", "\x11", None),
            ("Ctrl+C outside input", "\t\t\t\t\x03", None),
            ("SIGINT", None, signal.SIGINT),
            ("SIGTERM", None, signal.SIGTERM),
        ):
            with self.subTest(exit=name):
                terminal = Terminal()
                try:
                    terminal.wait_text("URL do servidor")
                    self.assertNotEqual(termios.tcgetattr(terminal.slave), terminal.original)
                finally:
                    restored = terminal.close(signal_number=signal_number, exit_key=exit_key)
                self.assertTrue(restored)
                self.assertEqual(terminal.process.returncode, 0)
                self.assertIn(b"\x1b[?2004l", terminal.output)
                self.assertIn(b"\x1b[?1049l", terminal.output)
                self.assertFalse(terminal.screen.cursor.hidden)

    def test_fatal_signals_restore_input_and_terminal_modes(self):
        for number in (signal.SIGABRT, signal.SIGSEGV):
            with self.subTest(signal=number):
                limit = resource.getrlimit(resource.RLIMIT_CORE)
                try:
                    resource.setrlimit(resource.RLIMIT_CORE, (0, limit[1]))
                    # Explicit signals exercise restoration independently of
                    # ASan's fault diagnostic handler (ordinary tests retain it).
                    options = (
                        os.environ.get("ASAN_OPTIONS", "detect_leaks=0")
                        + ":handle_segv=0:handle_abort=0"
                    )
                    terminal = Terminal(environment={"ASAN_OPTIONS": options})
                finally:
                    resource.setrlimit(resource.RLIMIT_CORE, limit)
                terminal.wait_text("URL do servidor")
                self.assertNotEqual(termios.tcgetattr(terminal.slave), terminal.original)
                self.assertTrue(terminal.close(signal_number=number))
                self.assertEqual(terminal.process.returncode, -number)
                for reset in (
                    b"\x1b[?2026l",
                    b"\x1b[?1002l",
                    b"\x1b[?1006l",
                    b"\x1b[?2004l",
                    b"\x1b[?1049l",
                ):
                    self.assertIn(reset, terminal.output)
                self.assertFalse(terminal.screen.cursor.hidden)

    def test_four_fields_resize_password_mask_and_real_request(self):
        with MockServer("private") as server:
            terminal = Terminal()
            try:
                terminal.wait_text("URL do servidor")
                terminal.send(server.url + "\tjosé\tSynthetic!123\tServer!123")
                terminal.resize(16, 40)
                terminal.resize(30, 100)
                terminal.send("\t\n")
                terminal.return_to_account()
                terminal.wait_text("Login realizado")
                self.assertEqual(
                    server.requests[2][3], {"username": "josé", "password": "Synthetic!123"}
                )
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
                terminal.return_to_account()
                terminal.wait_text("Login realizado")
                self.assertNotIn(b"Synthetic!123", terminal.output)
            finally:
                restored = terminal.close(signal_number=signal.SIGTERM)
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

    def test_slow_ws_and_event_burst_keep_exit_responsive(self):
        for scenario in ("ws-slow", "ws-queue"):
            with self.subTest(scenario=scenario), MockServer(scenario) as server:
                terminal = Terminal()
                try:
                    terminal.send(server.url + "\tjosé\tSynthetic!123\n")
                    deadline = time.monotonic() + 2
                    while (
                        not any(r[1].endswith("/ws") for r in server.requests)
                        and time.monotonic() < deadline
                    ):
                        terminal.read(0.03)
                    self.assertTrue(any(r[1].endswith("/ws") for r in server.requests))
                finally:
                    start = time.monotonic()
                    restored = terminal.close()
                self.assertLess(time.monotonic() - start, 0.4)
                self.assertTrue(restored)
                self.assertEqual(terminal.process.returncode, 0)

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
                wait_for_screen(
                    terminal,
                    lambda: "[ Entrar ]" in terminal.screen.display[18],
                    "Initial login frame did not arrive",
                )
                for index, value in enumerate([server.url, "josé", "Synthetic!123", "Server!123"]):
                    if index:
                        terminal.send("\t")
                    # Repeat past the beginning of both populated and empty inputs.
                    terminal.screen.visible_cursor_rows.clear()
                    terminal.send("apagar" + "\x7f" * 40 + "\x08" * 10 + value)
                    row, column = 6 + 3 * index, 12 + wcswidth(value)
                    self.assertEqual(terminal.wait_cursor(row, column), (row, column))
                    self.assertEqual(terminal.screen.cursor.y, 6 + 3 * index)
                    self.assertTrue(
                        all(row == 6 + 3 * index for row in terminal.screen.visible_cursor_rows)
                    )
                terminal.send("\n")
                terminal.return_to_account()
                terminal.wait_text("Login realizado")
                self.assertEqual(server.requests[1][3], {"server_password": "Server!123"})
                self.assertEqual(
                    server.requests[2][3], {"username": "josé", "password": "Synthetic!123"}
                )
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
                self.assertEqual(terminal.wait_cursor(9, 12), (9, 12))
                for _ in range(4):
                    terminal.send("\x7f" * 16)
                    self.assertEqual((terminal.screen.cursor.y, terminal.screen.cursor.x), (9, 12))
                terminal.send("\tSynthetic!123\n")
                terminal.return_to_account()
                terminal.wait_text("Login realizado")
                self.assertEqual(
                    server.requests[1][3], {"username": "josé", "password": "Synthetic!123"}
                )
            finally:
                restored = terminal.close()
            self.assertTrue(restored)

    def test_mouse_focus_fields_login_and_logout_after_resize(self):
        with MockServer("private") as server:
            terminal = Terminal()
            try:
                terminal.resize(30, 100)
                wait_for_screen(
                    terminal,
                    lambda: "Usuário (username)" in terminal.screen.display[11],
                    "Resized login form did not arrive",
                )
                # The centered card now begins at row 4 and column 18.
                for row, value in [
                    (9, server.url),
                    (12, "josé"),
                    (15, "Synthetic!123"),
                    (18, "Server!123"),
                ]:
                    terminal.click(row, 22)
                    terminal.send(value)
                    self.assertEqual(
                        terminal.wait_cursor(row, 22 + wcswidth(value)),
                        (row, 22 + wcswidth(value)),
                    )
                    self.assertEqual(terminal.screen.cursor.y, row)
                terminal.click(21, 24)
                terminal.return_to_account()
                terminal.wait_text("Login realizado")
                # Permission metadata shares this queue and may finish before
                # the account screen. Authentication itself must occur once.
                for endpoint in ("/auth/login_server", "/auth/login"):
                    self.assertEqual(
                        sum(r[0] == "POST" and r[1].endswith(endpoint) for r in server.requests), 1
                    )
                self.assertTrue(any(r[1].endswith("/auth/whoami") for r in server.requests))
                terminal.click(21, 24)
                terminal.wait_text("Você saiu da conta")
                self.assertEqual(
                    sum(r[0] == "POST" and r[1].endswith("/auth/logout") for r in server.requests),
                    1,
                )
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
                self.assertEqual(terminal.wait_cursor(9, 14), (9, 14))
                terminal.send("X")
                terminal.click(0, 0)
                terminal.read(0.2)
                self.assertEqual(terminal.wait_cursor(9, 15), (9, 15))
                terminal.send("\x7f\tSynthetic!123")
                terminal.click(18, 14)
                terminal.return_to_account()
                terminal.wait_text("Login realizado")
                self.assertEqual(server.requests[1][3]["username"], "josé")
            finally:
                restored = terminal.close()
            self.assertTrue(restored)


if __name__ == "__main__":
    test_main(
        1,
    )
