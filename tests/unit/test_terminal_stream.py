"""Terminal observer regression tests run identically on POSIX and ConPTY hosts."""

import os
import socket
import threading
import time
import unittest
from unittest.mock import patch
from test_context import main as test_main
import terminal_support
from terminal_support import ObservedScreen, PosixTerminal, TerminalStream

POSIX_READS = "POSIX pseudo-terminal reads; test_terminal_windows.py covers the ConPTY ones"


class TerminalStreamTests(unittest.TestCase):
    def test_kitty_graphics_query_never_changes_text_or_cursor(self):
        query = b"\x1b_Gi=1347440639,s=1,v=1,a=q,t=d,f=24;AAAA\x1b\\"
        data = b"\x1b[2;3H" + query + "josé".encode()
        for split in range(len(data) + 1):
            with self.subTest(split=split):
                screen = ObservedScreen(12, 3)
                stream = TerminalStream(screen)
                stream.feed(data[:split])
                stream.feed(data[split:])
                self.assertEqual(screen.display[1].rstrip(), "  josé")
                self.assertEqual((screen.cursor.y, screen.cursor.x), (1, 6))

    def test_saved_cursor_outside_scroll_region_keeps_absolute_row(self):
        for row in (1, 9):
            data = f"\x1b[3;5r\x1b[{row};4H\x1b7\x1b[4;2H\x1b8Z".encode()
            for split in range(len(data) + 1):
                with self.subTest(row=row, split=split):
                    screen = ObservedScreen(12, 10)
                    stream = TerminalStream(screen)
                    stream.feed(data[:split])
                    stream.feed(data[split:])
                    self.assertEqual(screen.display[row - 1].rstrip(), "   Z")
                    self.assertEqual((screen.cursor.y, screen.cursor.x), (row - 1, 4))

    def test_fragmented_graphics_payload_is_discarded_without_buffering_it(self):
        screen = ObservedScreen(12, 2)
        stream = TerminalStream(screen)
        stream.feed(b"before\x1b_")
        for _ in range(100):
            stream.feed(b"A" * 1024)
            self.assertEqual(stream._apc_tail, b"")
        stream.feed(b"\x1b")
        stream.feed(b"\\after")
        self.assertEqual(screen.display[0].rstrip(), "beforeafter")
        self.assertEqual((screen.cursor.y, screen.cursor.x), (0, 11))

    def test_windows_line_drawing_and_utf8_survive_every_read_boundary(self):
        data = "\x1b(0lqk\x1b(B josé 😀\r\n\x1b(0mqj\x1b(B".encode()
        for split in range(len(data) + 1):
            with self.subTest(split=split):
                screen = ObservedScreen(24, 2)
                stream = TerminalStream(screen, ansi_charset=True)
                stream.feed(data[:split])
                stream.feed(data[split:])
                self.assertEqual(screen.display[0].rstrip(), "┌─┐ josé 😀")
                self.assertEqual(screen.display[1].rstrip(), "└─┘")

    def test_windows_observer_keeps_ascii_after_leaving_line_drawing(self):
        screen = ObservedScreen(8, 2)
        stream = TerminalStream(screen, ansi_charset=True)
        for byte in b"\x1b(0j\x1b(Bj":
            stream.feed(bytes([byte]))
        self.assertEqual(screen.display[0].rstrip(), "┘j")


class PosixTerminalReads(unittest.TestCase):
    def terminal(self):
        receiver, sender = socket.socketpair()
        self.addCleanup(receiver.close)
        self.addCleanup(sender.close)
        terminal = PosixTerminal.__new__(PosixTerminal)
        terminal.master = receiver.fileno()
        terminal.output = bytearray()
        terminal.arrivals = []
        terminal.screen = ObservedScreen(40, 2)
        terminal.stream = TerminalStream(terminal.screen)
        return terminal, sender

    @unittest.skipIf(os.name == "nt", POSIX_READS)
    def test_a_redraw_that_arrives_in_pieces_is_read_whole(self):
        terminal, sender = self.terminal()
        sender.sendall(b"top")
        # The second piece comes after the read's time is up, before the pause that ends it.
        late = threading.Timer(0.1, sender.sendall, [b" bottom"])
        late.start()
        self.addCleanup(late.join)
        with patch.object(terminal_support, "FRAME_PAUSE", 0.5):
            terminal.read(0.03)
        self.assertEqual(terminal.screen.display[0].rstrip(), "top bottom")
        self.assertEqual([end for _, end in terminal.arrivals], [3, 10])

    @unittest.skipIf(os.name == "nt", POSIX_READS)
    def test_output_that_never_pauses_extends_a_read_by_a_limited_time(self):
        terminal, sender = self.terminal()
        playing = threading.Event()

        def video():
            while not playing.wait(0.005):
                sender.sendall(b".")

        writer = threading.Thread(target=video)
        writer.start()
        self.addCleanup(writer.join)
        self.addCleanup(playing.set)
        with (
            patch.object(terminal_support, "FRAME_PAUSE", 0.2),
            patch.object(terminal_support, "FRAME_LIMIT", 0.3),
        ):
            started = time.monotonic()
            terminal.read(0.03)
            lasted = time.monotonic() - started
        self.assertGreaterEqual(lasted, 0.3)
        self.assertLess(lasted, 2)

    @unittest.skipIf(os.name == "nt", POSIX_READS)
    def test_a_read_without_output_lasts_only_its_time(self):
        terminal, _ = self.terminal()
        with patch.object(terminal_support, "FRAME_PAUSE", 5):
            started = time.monotonic()
            terminal.read(0.03)
        self.assertLess(time.monotonic() - started, 1)


if __name__ == "__main__":
    test_main(0)
