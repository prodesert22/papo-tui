import os

if os.name != "nt":
    import fcntl
    import pty
    import resource
    import termios
import os
import select
import signal
import re
import struct
import subprocess
import sys
import time
import tempfile
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

    def scroll_up(self, count=1):
        saved_y = self.cursor.y
        self.cursor.y = self.margins.bottom if self.margins else self.lines - 1
        for _ in range(min(count or 1, self.lines)):
            self.index()
        self.cursor.y = saved_y

    def scroll_down(self, count=1):
        saved_y = self.cursor.y
        self.cursor.y = self.margins.top if self.margins else 0
        for _ in range(min(count or 1, self.lines)):
            self.reverse_index()
        self.cursor.y = saved_y


class TerminalStream(pyte.ByteStream):
    # ncurses uses SU/SD within DECSTBM when reflowing history. pyte's default
    # parser omits these standard controls; ignoring them loses visible lines.
    csi = dict(pyte.Stream.csi, S="scroll_up", T="scroll_down")
    events = pyte.Stream.events | {"scroll_up", "scroll_down"}

    def feed(self, data):
        # pyte omits colon-form SGR used by xterm-direct terminfo. Normalize
        # equivalent color parameters, preserving CSI split across PTY reads.
        data = getattr(self, "_sgr_pending", b"") + data
        tail = re.search(rb"(?:\x1b\[[0-9:;]*|\x1b)$", data)
        self._sgr_pending = data[tail.start() :] if tail else b""
        if tail:
            data = data[: tail.start()]

        def normalize(match):
            parameters = re.sub(rb"(38|48):2::?", rb"\1;2;", match[1])
            return b"\x1b[" + parameters.replace(b":", b";") + b"m"

        super().feed(re.sub(rb"\x1b\[([0-9:;]*)m", normalize, data))


def editor_region(terminal):
    """Locate editable rows; exclude input padding/borders, typing and footer."""
    column = 27 if terminal.screen.columns >= 80 else 2
    first = next(
        row
        for row in range(5, terminal.screen.lines - 4)
        if terminal.screen.buffer[row][column].data == ">"
    )
    return first, terminal.screen.lines - (6 if terminal.screen.lines >= 18 else 5)


def editor_column(terminal):
    """Text begins after the editor prompt; toolbar buttons sit on the right."""
    prompt = 27 if terminal.screen.columns >= 80 else 2
    return prompt + 2


class PosixTerminal:
    def __init__(
        self,
        arguments=(),
        *,
        terminal_type="xterm-256color",
        environment=None,
        allow_local_http=True,
        unset_environment=(),
    ):
        self.master, self.slave = pty.openpty()
        self.original = termios.tcgetattr(self.slave)
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
        env = dict(os.environ, TERM=terminal_type, LANG="C.UTF-8", LC_ALL="C.UTF-8")
        for key in unset_environment:
            env.pop(key, None)
        # Existing PTY suites must never open the user's personal wallet.
        env["DBUS_SESSION_BUS_ADDRESS"] = "unix:path=/nonexistent/papo-test-bus"
        self.configuration = tempfile.TemporaryDirectory(prefix="papo-terminal-config-")
        # PulseAudio may create its own runtime directory. Keep it outside the
        # media fixture directory whose cleanup is verified by player tests.
        env["PULSE_RUNTIME_PATH"] = os.path.join(self.configuration.name, "pulse")
        os.makedirs(env["PULSE_RUNTIME_PATH"], mode=0o700)
        env["PULSE_SERVER"] = "unix:/nonexistent/papo-test-pulse"
        env["XDG_CONFIG_HOME"] = self.configuration.name
        env["XDG_STATE_HOME"] = os.path.join(self.configuration.name, "state")
        env.update(environment or {})
        # Never change the personal desktop clipboard from synthetic PTY tests.
        if not environment or "WAYLAND_DISPLAY" not in environment:
            env["WAYLAND_DISPLAY"] = ""
        if not environment or "DISPLAY" not in environment:
            env["DISPLAY"] = ""
        command = [BINARY, *arguments]
        if allow_local_http and "--allow-local-http" not in arguments:
            command.append("--allow-local-http")
        self.process = subprocess.Popen(
            command,
            stdin=self.slave,
            stdout=self.slave,
            stderr=self.slave,
            env=env,
            start_new_session=True,
        )
        self.output = bytearray()
        self.screen = ObservedScreen(80, 24)
        self.stream = TerminalStream(self.screen)
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
            raise AssertionError(
                f"Texto esperado ausente: {text}; fim da saída={bytes(self.output[-1000:])!r}"
            )

    def click(self, row, column):
        # SGR mouse reports, parsed by ncurses rather than by application code.
        self.send(f"\x1b[<0;{column + 1};{row + 1}M\x1b[<0;{column + 1};{row + 1}m")

    def wait_cursor(self, row, column, seconds=3):
        expected = (row, column)
        deadline = time.monotonic() + seconds
        while (
            self.screen.cursor.y,
            self.screen.cursor.x,
        ) != expected and time.monotonic() < deadline:
            self.read(0.03)
        return self.screen.cursor.y, self.screen.cursor.x

    def return_to_account(self):
        """Explicitly return with F6 for tests of account-only controls."""
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            display = "\n".join(self.screen.display)
            if "[+ Adicionar]" in display and ("F6: conta" in display or "[Conversa]" in display):
                self.send("\x1b[17~")
                deadline = time.monotonic() + 5
                while (
                    "[Canais / F6]" not in "\n".join(self.screen.display)
                    and time.monotonic() < deadline
                ):
                    self.read(0.03)
                if "[Canais / F6]" not in "\n".join(self.screen.display):
                    raise AssertionError("F6 did not return to the account screen")
                return
            self.read(0.03)
        raise AssertionError("Expected an authenticated account or channel screen")

    def close(self, signal_number=None, exit_key="\x11"):
        if self.process.poll() is None:
            if signal_number is not None:
                self.process.send_signal(signal_number)
            else:
                self.send(exit_key)
            try:
                # Keep acting as the terminal while the client finishes a
                # pending image transfer; an unread PTY can block its exit.
                deadline = time.monotonic() + 5
                while self.process.poll() is None and time.monotonic() < deadline:
                    self.read(0.03)
                self.process.wait(timeout=max(0.01, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
                raise
        self.read()
        restored = termios.tcgetattr(self.slave) == self.original
        os.close(self.master)
        os.close(self.slave)
        self.configuration.cleanup()
        return restored


if os.name == "nt":
    from terminal_windows import WindowsTerminal

    class Terminal(WindowsTerminal, PosixTerminal):
        pass
else:
    Terminal = PosixTerminal


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
            self.assertIn("papo", terminal.screen.display[2])
            self.assertIn("Usuário (username)", terminal.screen.display[8])
            self.assertIn("[ Entrar ]", terminal.screen.display[18])
            for row in (4, 17):
                # pyte leaves DEC line drawing as 'q' in UTF-8 mode.
                rule = terminal.screen.display[row][10:70]
                self.assertIn(rule, ("q" * 60, "─" * 60))
            terminal.send("\tação界語")
            self.assertIn("ação界語", terminal.screen.display[9])
            self.assertEqual((terminal.screen.cursor.y, terminal.screen.cursor.x), (9, 20))
            terminal.resize(16, 40)
            self.assertIn("Amplie o terminal", terminal.screen.display[0])
            terminal.resize(2, 18)
            self.assertEqual(terminal.screen.display[1].rstrip(), "Ctrl+Q: sair")
            terminal.resize(22, 44)
            self.assertIn("ação界語", terminal.screen.display[8])
            self.assertEqual((terminal.screen.cursor.y, terminal.screen.cursor.x), (8, 14))
            terminal.resize(30, 100)
            self.assertIn("ação界語", terminal.screen.display[12])
            self.assertEqual((terminal.screen.cursor.y, terminal.screen.cursor.x), (12, 30))
            terminal.send("\x7f")
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
                for index, value in enumerate([server.url, "josé", "Synthetic!123", "Server!123"]):
                    if index:
                        terminal.send("\t")
                    # Repeat past the beginning of both populated and empty inputs.
                    terminal.screen.visible_cursor_rows.clear()
                    terminal.send("apagar" + "\x7f" * 40 + "\x08" * 10 + value)
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
                # The centered card now begins at row 4 and column 18.
                for row, value in [
                    (9, server.url),
                    (12, "josé"),
                    (15, "Synthetic!123"),
                    (18, "Server!123"),
                ]:
                    terminal.click(row, 22)
                    terminal.send(value)
                    self.assertEqual(terminal.screen.cursor.y, row)
                terminal.click(21, 24)
                terminal.return_to_account()
                terminal.wait_text("Login realizado")
                self.assertEqual(
                    len(
                        [
                            r
                            for r in server.requests
                            if not r[1].split("?")[0].endswith(("/dms", "/notifications", "/ws"))
                        ]
                    ),
                    5,
                )
                terminal.click(21, 24)
                terminal.wait_text("Você saiu da conta")
                self.assertEqual(
                    len(
                        [
                            r
                            for r in server.requests
                            if not r[1].split("?")[0].endswith(("/dms", "/notifications", "/ws"))
                        ]
                    ),
                    6,
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
    unittest.main()
