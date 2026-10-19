"""Isolated POSIX/ConPTY transport and terminal protocol observers."""

from test_context import argument, terminals
import os

if os.name != "nt":
    import fcntl
    import pty
    import termios
import os
import select
import signal
import re
import struct
import subprocess
import time
import tempfile

import pyte


BINARY = argument(0)


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

    def __init__(self, *args, ansi_charset=False, **kwargs):
        super().__init__(*args, **kwargs)
        self.ansi_charset = ansi_charset
        if ansi_charset:
            # ConPTY preserves DEC line drawing alongside UTF-8 text. pyte
            # otherwise ignores SCS in UTF-8 mode, showing `j` instead of ┘.
            self.use_utf8 = False

    def feed(self, data):
        # pyte does not consume APC strings such as Kitty graphics queries.
        # They are nonprinting, including when their opener or ST terminator
        # is split across reads. Retain at most one byte of a partial marker.
        data = getattr(self, "_apc_tail", b"") + data
        self._apc_tail = b""
        visible = []
        while data:
            inside = getattr(self, "_apc_open", False)
            marker = b"\x1b\\" if inside else b"\x1b_"
            end = data.find(marker)
            if end < 0:
                if data.endswith(b"\x1b"):
                    self._apc_tail = b"\x1b"
                    data = data[:-1]
                if not inside:
                    visible.append(data)
                break
            if not inside:
                visible.append(data[:end])
            self._apc_open = not inside
            data = data[end + len(marker) :]
        data = b"".join(visible)
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

        data = re.sub(rb"\x1b\[([0-9:;]*)m", normalize, data)
        if self.ansi_charset:
            # Decode bytes incrementally without disabling the parser's SCS.
            pyte.Stream.feed(self, self.utf8_decoder.decode(data))
        else:
            super().feed(data)


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


def wait_for_screen(terminal, predicate, description, seconds=5):
    """Observe a postcondition; sending input is not a rendering barrier."""
    deadline = time.monotonic() + seconds
    while True:
        try:
            if predicate():
                return
        except StopIteration:
            # An incremental repaint may temporarily omit the editor prompt.
            pass
        if time.monotonic() >= deadline:
            raise AssertionError(description + "\n" + "\n".join(terminal.screen.display))
        terminal.read(0.03)


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
        for key in (
            *unset_environment,
            "TERM_PROGRAM",
            "LC_TERMINAL",
            "KITTY_PID",
            "KITTY_WINDOW_ID",
            "KONSOLE_VERSION",
            "VTE_VERSION",
            "GHOSTTY_BIN_DIR",
            "WEZTERM_EXECUTABLE",
            "TMUX",
            "STY",
            "COLORTERM",
            "HTTP_PROXY",
            "HTTPS_PROXY",
            "ALL_PROXY",
            "http_proxy",
            "https_proxy",
            "all_proxy",
        ):
            env.pop(key, None)
        env["NO_PROXY"] = "127.0.0.1,localhost,::1"
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
        env["XDG_CACHE_HOME"] = os.path.join(self.configuration.name, "cache")
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
        self._closed = False
        terminals.append(self)
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
        if self._closed:
            return self._restored
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
        self._closed = True
        self._restored = restored
        return restored


if os.name == "nt":
    from terminal_windows import WindowsTerminal

    class Terminal(WindowsTerminal, PosixTerminal):
        pass
else:
    Terminal = PosixTerminal
