"""Terminal observer regression tests run identically on POSIX and ConPTY hosts."""

import unittest
from test_context import main as test_main
from terminal_support import ObservedScreen, TerminalStream


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


if __name__ == "__main__":
    test_main(0)
