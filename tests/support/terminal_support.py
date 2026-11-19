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
from wcwidth import wcwidth

from terminal_keys import CTRL_J, encode_keys  # noqa: F401


BINARY = argument(0)


# What pyte reports for the fills of a server or Canais/DMs tab: the selected
# one (the "selected conversation" fill) and the keyboard cursor on one (the
# focus highlight). xterm-256color redefines colors 240 + role, in either palette;
# Kitty keeps the indexed xterm ones, dark and light.
TAB_SELECTED_BACKGROUNDS = {"b2b2b2", "262626", "ffd7af"}
TAB_CURSOR_BACKGROUNDS = {"d0d0d0", "875f00", "d7af5f"}
# The tabs above the conversation list, in Portuguese and English, and the
# unread dot one of them may carry after its name.
LIST_TABS = {"Canais", "Channels", "DMs"}
UNREAD_DOTS = "⦁•"


class ObservedScreen(pyte.Screen):
    def __init__(self, *args):
        self.visible_cursor_rows = []
        super().__init__(*args)

    @property
    def display(self):
        """The screen text, with the server and Canais/DMs tabs written as markers.

        These tabs are blocks of fill, so the text alone cannot tell which one
        is selected. Their padding cells keep the columns the old markers had:
        the selected tab shows as [name] and a tab under the keyboard cursor as
        >name<, as the application still draws the server tabs on terminals
        with fewer than 256 colors. A tab that is neither keeps its bare name.
        Tests that need the real colors read ``buffer``."""
        lines = super().display
        if self.lines:
            lines[0] = self._tab_markers(0, lines[0])
        # The conversation list starts below the tab rows and the frame's edge.
        row = 2 if self.columns >= 80 else 3
        if row < self.lines:
            lines[row] = self._tab_markers(row, lines[row], LIST_TABS)
        return lines

    def _tab_markers(self, y, text, names=None):
        row = self.buffer[y]
        runs, start = [], None
        for x in range(self.columns + 1):
            background = row[x].bg if x < self.columns else None
            if background in TAB_SELECTED_BACKGROUNDS | TAB_CURSOR_BACKGROUNDS:
                if start is None or row[start].bg != background:
                    if start is not None:
                        runs.append((start, x - 1))
                    start = x
            elif start is not None:
                runs.append((start, x - 1))
                start = None
        cells = [row[x].data for x in range(self.columns)]
        if names:
            # The same fills mark other controls of this row; keep the tabs only.
            runs = [
                (first, last)
                for first, last in runs
                if "".join(cells[first + 1 : last]).strip(" " + UNREAD_DOTS) in names
            ]
            if not runs:
                return text
        selected = [run for run in runs if row[run[0]].bg in TAB_SELECTED_BACKGROUNDS]
        for first, last in runs:
            label = "".join(cells[first + 1 : last]).strip()
            if row[first].bg in TAB_SELECTED_BACKGROUNDS:
                opening, closing = "[", "]"
            elif label == "+ Adicionar":
                opening, closing = ">", "<"
            else:
                # The cursor on the selected tab hides its own fill.
                opening, closing = ("[", "]") if not selected else (">", "<")
            if cells[first] == " " and cells[last] == " " and first != last:
                cells[first], cells[last] = opening, closing
        shown, skip = [], False
        for x in range(self.columns):
            if skip:
                skip = False
                continue
            shown.append(cells[x])
            skip = bool(cells[x]) and wcwidth(cells[x][0]) == 2
        return "".join(shown) if shown else text

    def cursor_position(self, *args, **kwargs):
        super().cursor_position(*args, **kwargs)
        if not self.cursor.hidden:
            self.visible_cursor_rows.append(self.cursor.y)

    def restore_cursor(self):
        # pyte clamps DECRC to the scroll region even in absolute cursor mode.
        # Pixel transfers save/restore below it (for example, player controls).
        saved_row = (
            self.savepoints[-1].cursor.y
            if self.savepoints and not self.savepoints[-1].origin
            else None
        )
        super().restore_cursor()
        if saved_row is not None:
            self.cursor.y = max(0, min(self.lines - 1, saved_row))

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


# The footer carries the call buttons only in a build with the WebRTC stack.
VOICE_BUTTONS = ("[🔊]", "[🔇]", "[♪]", "[♪×]", "[🎤]", "[🎤×]", "[●]", "[●×]")


def voice_supported(terminal):
    """Report whether the running program was built with voice support."""
    screen = "\n".join(terminal.screen.display)
    return any(marker in screen for marker in VOICE_BUTTONS)


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
        erase_char=None,
    ):
        self.master, self.slave = pty.openpty()
        self.original = termios.tcgetattr(self.slave)
        if erase_char is not None:
            attributes = termios.tcgetattr(self.slave)
            attributes[6][termios.VERASE] = bytes([erase_char])
            termios.tcsetattr(self.slave, termios.TCSANOW, attributes)
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
            "TMUX_PANE",
            "STY",
            # A clicked notification raises the terminal through these; tests
            # must never focus windows of the session running them.
            "XDG_CURRENT_DESKTOP",
            "SWAYSOCK",
            "I3SOCK",
            "HYPRLAND_INSTANCE_SIGNATURE",
            "KONSOLE_DBUS_SERVICE",
            "KONSOLE_DBUS_WINDOW",
            "KONSOLE_DBUS_SESSION",
            "WEZTERM_PANE",
            "WEZTERM_EXECUTABLE_DIR",
            "WINDOWID",
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
        # No suite asks GitHub for releases unless it serves the answer itself.
        env["PAPO_UPDATE_CHECK"] = "0"
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
        # (monotonic time, output length) after each PTY read, for timing
        # assertions that must not depend on when a wait loop polls.
        self.arrivals = []
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
                arrived = time.monotonic()
                self.output.extend(data)
                self.arrivals.append((arrived, len(self.output)))
                self.stream.feed(data)

    def arrival(self, index):
        """Monotonic time when output byte index was read from the PTY."""
        for arrived, end in self.arrivals:
            if index < end:
                return arrived
        raise IndexError(index)

    def send(self, value):
        keys, self._pasting = encode_keys(value, getattr(self, "_pasting", False))
        os.write(self.master, keys.encode())
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
        """Explicitly return with Ctrl+T for tests of account-only controls."""
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            display = "\n".join(self.screen.display)
            if "+ Adicionar" in display and ("Foco:" in display or "[Conversa]" in display):
                self.send("\x14")
                deadline = time.monotonic() + 5
                while (
                    "[Canais]" not in "\n".join(self.screen.display) and time.monotonic() < deadline
                ):
                    self.read(0.03)
                if "[Canais]" not in "\n".join(self.screen.display):
                    raise AssertionError("Ctrl+T did not return to the account screen")
                return
            self.read(0.03)
        raise AssertionError("Expected an authenticated account or channel screen")

    def close(self, signal_number=None, exit_key="\x11"):
        if self._closed:
            return self._restored
        self._screen_before_close = list(self.screen.display)
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


# DEC line drawing as a screen line holds it: the POSIX stream ignores character set switches and
# keeps the ASCII letters, while ConPTY's output reaches pyte as box drawing characters.
VERTICAL = "│" if os.name == "nt" else "x"
HORIZONTAL = "─" if os.name == "nt" else "q"

if os.name == "nt":
    from terminal_windows import WindowsTerminal

    class Terminal(WindowsTerminal, PosixTerminal):
        pass
else:
    Terminal = PosixTerminal
