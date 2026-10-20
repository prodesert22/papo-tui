"""Modal interactions over a local PTY, without a server or session credentials."""

from test_context import main as test_main

import unittest
from terminal_support import Terminal, wait_for_screen


class ModalTest(unittest.TestCase):
    def wait(self, t, text, absent=False):
        wait_for_screen(
            t,
            lambda: (text in "\n".join(t.screen.display)) != absent,
            f"Unexpected modal state for: {text}",
        )

    def button_position(self, t):
        for y, line in enumerate(t.screen.display):
            x = line.find("[Info")
            if x >= 0:
                return y, x + 1
        self.fail("Info button is missing from the modal")

    def resize(self, t, rows, columns):
        previous = self.button_position(t)
        t.resize(rows, columns)
        wait_for_screen(
            t,
            lambda: (
                any("[Info" in line for line in t.screen.display)
                and self.button_position(t) != previous
            ),
            "Resized media button did not arrive",
        )

    def test_properties_button_keys_resize_and_outside_click(self):
        t = Terminal(arguments=("--demo",))
        try:
            self.wait(t, "Modal fechado")
            t.send("\x1b[18~")
            self.wait(t, "Modal aberto")
            self.assertIn("Canais preservados", "\n".join(t.screen.display))
            self.wait(t, "[Info]")
            button = self.button_position(t)
            t.click(*button)
            self.wait(t, "Propriedades")
            self.wait(t, "43562 bytes")
            self.wait(t, "image/gif")
            self.assertEqual(self.button_position(t), button)
            t.click(*button)
            self.wait(t, "Propriedades", absent=True)
            t.click(*button)
            self.wait(t, "Propriedades")
            self.assertNotIn(b"\x1b]52;", t.output)
            t.send("\x1b[6~")
            self.wait(t, "Dimensões")
            t.send("i")
            self.wait(t, "Propriedades", absent=True)
            t.send("\t\n")
            self.wait(t, "Propriedades")
            t.send("\x1b[19~")
            self.wait(t, "Propriedades", absent=True)
            t.send("\x1b[19~")
            self.wait(t, "Propriedades")
            t.send("\x1b[<65;65;12M")
            self.wait(t, "Nome", absent=True)
            self.resize(t, 14, 44)
            self.wait(t, "Propriedades")
            button = self.button_position(t)
            t.click(*button)
            self.wait(t, "Propriedades", absent=True)
            self.assertEqual(self.button_position(t), button)
            t.click(*button)
            self.wait(t, "Propriedades")
            t.send("\x1b[6~")
            t.send("\x1b[5~")
            self.resize(t, 30, 100)
            self.wait(t, "Propriedades")
            button = self.button_position(t)
            t.click(*button)
            self.wait(t, "Propriedades", absent=True)
            t.click(*button)
            self.wait(t, "Propriedades")
            t.click(8, 4)
            self.wait(t, "Modal fechado")
            self.assertIn("Canais acionados: 0", "\n".join(t.screen.display))
            t.send("\x1b[18~")
            self.wait(t, "Modal aberto")
            self.assertNotIn("Propriedades", "\n".join(t.screen.display))
            t.send("\x1b")
            self.wait(t, "Modal fechado")
        finally:
            self.assertTrue(t.close())
            self.assertEqual(t.process.returncode, 0)
            for diagnostic in (b"AddressSanitizer", b"runtime error:"):
                self.assertNotIn(diagnostic, t.output)


if __name__ == "__main__":
    test_main(
        1,
    )
