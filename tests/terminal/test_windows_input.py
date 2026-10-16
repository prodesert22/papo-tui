"""Locate Unicode loss between ConPTY, the native console and ncurses."""

import json
import re
import select
import sys
import time
import unittest
from winpty import Backend, PtyProcess

BINARY = sys.argv.pop(1)


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
            payload = "aé界😀z"
            child.write(payload + "\r")
            until("PAPO_CURSES_READY")
            child.write(payload + "\x1b")
            until("PAPO_PROBE_RESULT=")
            until("}")
            report = json.loads(re.search(r"PAPO_PROBE_RESULT=(\{[^\r\n]+\})", output)[1])
            native = b"".join(key.to_bytes(2, "little") for key in report["native"]).decode(
                "utf-16-le"
            )
            self.assertEqual(native, payload, json.dumps(report))
            self.assertEqual(report["curses"], list(map(ord, payload)), json.dumps(report))
            self.assertTrue(report["iswprint_e_acute"], json.dumps(report))
        finally:
            if child.isalive():
                child.terminate(force=True)


if __name__ == "__main__":
    unittest.main()
