"""Verify Unicode, keys and VT output modes through the actual console."""

from test_context import main as test_main

from test_context import argument

import json
import re
import select
import time
import unittest
from winpty import Backend, PtyProcess
from terminal_windows import close_pty_process

BINARY = argument(0)


class WindowsInput(unittest.TestCase):
    def test_native_and_ncurses_preserve_unicode(self):
        child = PtyProcess.spawn([BINARY], backend=Backend.ConPTY, dimensions=(24, 80))
        output = ""

        def until(text):
            nonlocal output
            deadline = time.monotonic() + 8
            while text not in output and time.monotonic() < deadline:
                if select.select([child.fd], [], [], 0.02)[0]:
                    try:
                        output += child.read(65536)
                    except EOFError:
                        break
            self.assertIn(text, output)

        try:
            until("PAPO_NATIVE_READY")
            # U+0103 collides with KEY_UP in the native console driver.
            payload = "aéăŁ界😀z"
            child.write(payload + "\r")
            until("PAPO_CURSES_READY")
            child.write(payload + "\x1b")
            until("PAPO_COMMANDS_READY")
            child.write("aaabb\x1bOA\x1bOA\x1b[200~x\ny\x1b[201~\x04")
            until("PAPO_MOUSE_READY")
            child.write(
                "\x1b[<0;5;6M\x1b[<0;5;6m"
                "\x1b[<2;8;9M\x1b[<2;8;9m"
                "\x1b[<64;10;11M\x1b[<65;10;11M"
                "\x1b[<0;300;12M\x1b[<0;300;12m"
            )
            until("PAPO_PROBE_RESULT=")
            until("}")
            report = json.loads(re.search(r"PAPO_PROBE_RESULT=(\{[^\r\n]+\})", output)[1])
            native = b"".join(key.to_bytes(2, "little") for key in report["native"]).decode(
                "utf-16-le"
            )
            self.assertEqual(native, payload, json.dumps(report))
            self.assertEqual(report["curses"], list(map(ord, payload)), json.dumps(report))
            self.assertEqual(
                report["repeats"],
                [ord("a")] * 3 + [ord("é")] * 2 + [ord("z")],
                json.dumps(report),
            )
            self.assertEqual(
                report["commands"],
                [ord("a")] * 3
                + [ord("b")] * 2
                + [0x110000] * 2
                + [0x110001, ord("x"), 10, ord("y"), 0x110002],
                report,
            )
            self.assertTrue(report["vt_input"], report)
            self.assertEqual(report["widths"], [1, 1, 1, 1, 2, 2, 1, 1], report)
            self.assertEqual(
                report["mouse"],
                [
                    [4, 5, 1],
                    [4, 5, 2],
                    [7, 8, 3],
                    [7, 8, 4],
                    [9, 10, 5],
                    [9, 10, 6],
                    [299, 11, 1],
                    [299, 11, 2],
                ],
                report,
            )
            self.assertTrue(report["vt_output"], report)
            self.assertTrue(report["processed_output"], report)
            self.assertTrue(report["iswprint_e_acute"], json.dumps(report))
        finally:
            close_pty_process(child)


if __name__ == "__main__":
    test_main(
        1,
    )
