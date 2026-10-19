"""ConPTY backend. Shared terminal tests provide navigation and assertions."""

import os
import select
import tempfile
import time
from winpty import Backend, PtyProcess


def close_pty_process(child):
    try:
        child.close(force=True)
    finally:
        # pywinpty 3.1 marks closed in isalive() after process exit, causing
        # close() to skip these sockets. Explicit close is safe in either case.
        child.fileobj.close()
        child._server.close()


class Process:
    def __init__(self, child):
        self.child = child
        self.pid = child.pid

    def poll(self):
        return None if self.child.isalive() else (self.child.exitstatus or 0)

    def send_signal(self, number):
        self.child.kill(number)


class WindowsTerminal:
    def __init__(
        self,
        arguments=(),
        *,
        terminal_type="xterm-256color",
        environment=None,
        allow_local_http=True,
        unset_environment=(),
    ):
        # Imported lazily: test_terminal defines the shared pyte observer first.
        from terminal_support import BINARY, ObservedScreen, TerminalStream

        self.configuration = tempfile.TemporaryDirectory(prefix="papo-conpty-")
        self._closed = False
        self._clean_exit = False
        env = dict(
            os.environ,
            TERM=terminal_type,
            XDG_CONFIG_HOME=self.configuration.name,
            XDG_STATE_HOME=os.path.join(self.configuration.name, "state"),
            XDG_CACHE_HOME=os.path.join(self.configuration.name, "cache"),
            PYTHONUTF8="1",
        )
        # Every test has its own config scope; it cannot read personal credentials.
        env.update(environment or {})
        for key in unset_environment:
            env.pop(key, None)
        args = [BINARY, *arguments]
        if allow_local_http and "--allow-local-http" not in args:
            args.append("--allow-local-http")
        try:
            self.child = PtyProcess.spawn(
                args, env=env, dimensions=(24, 80), backend=Backend.ConPTY
            )
        except BaseException:
            self.configuration.cleanup()
            raise
        self.process = Process(self.child)
        self.output = bytearray()
        from test_context import terminals

        terminals.append(self)
        self.screen = ObservedScreen(80, 24)
        self.stream = TerminalStream(self.screen, ansi_charset=True)
        try:
            self.read(0.2)
        except BaseException:
            self.close()
            raise

    def read(self, seconds=0.1):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            ready, _, _ = select.select([self.child.fd], [], [], 0.02)
            # PtyProcess.read() blocks in recv(1) until its entire chunk
            # decodes as UTF-8. Keep incomplete code points in ByteStream
            # instead, so the caller's deadline still bounds every read.
            data = self.child.fileobj.recv(65536) if ready else None
            if data == b"":
                break
            if data == b"0011Ignore":
                continue  # pywinpty's empty-read sentinel.
            if data:
                self.output.extend(data)
                self.stream.feed(data)
            else:
                time.sleep(0.01)

    def send(self, value):
        self.child.write(value)
        self.read()

    def resize(self, rows, cols):
        self.screen.resize(lines=rows, columns=cols)
        self.child.setwinsize(rows, cols)
        self.read()

    def close(self, signal_number=None, exit_key="\x11"):
        if self._closed:
            return self._clean_exit
        if hasattr(self, "screen"):
            self._screen_before_close = list(self.screen.display)
        try:
            if self.child.isalive():
                if signal_number is None:
                    self.child.write(exit_key)
                else:
                    self.child.kill(signal_number)
            deadline = time.monotonic() + 8
            while self.child.isalive() and time.monotonic() < deadline:
                self.read(0.03)
            self._clean_exit = not self.child.isalive()
            return self._clean_exit
        finally:
            try:
                close_pty_process(self.child)
            finally:
                # The process must release its session lock before rmtree.
                if not self.child.isalive():
                    self.configuration.cleanup()
                    self._closed = True
