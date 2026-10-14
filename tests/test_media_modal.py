"""Modal interactions over a local PTY, without a server or session credentials."""
import sys
import time
import unittest
from test_terminal import Terminal
class ModalTest(unittest.TestCase):
    def wait(self, t, text):
        deadline = time.monotonic() + 5
        while text not in '\n'.join(t.screen.display) and time.monotonic() < deadline: t.read(.03)
        self.assertIn(text, '\n'.join(t.screen.display))
    def button_position(self, t):
        for y, line in enumerate(t.screen.display):
            x = line.find('[Info')
            if x >= 0: return y, x + 1
        self.fail('Info button is missing from the modal')
    def test_properties_button_keys_resize_and_outside_click(self):
        t = Terminal(arguments=('--demo',))
        try:
            self.wait(t, 'Modal fechado')
            t.send('\x1b[18~'); self.wait(t, 'Modal aberto')
            self.assertIn('Canais preservados', '\n'.join(t.screen.display))
            self.wait(t, '[Info]')
            button = self.button_position(t)
            t.click(*button); self.wait(t, 'Propriedades')
            self.wait(t, '43562 bytes'); self.wait(t, 'image/gif')
            self.assertEqual(self.button_position(t), button)
            t.click(*button); t.read(.1)
            self.assertNotIn('Propriedades', '\n'.join(t.screen.display))
            t.click(*button); self.wait(t, 'Propriedades')
            self.assertNotIn(b'\x1b]52;', t.output)
            t.send('\x1b[6~'); self.wait(t, 'Dimensões')
            t.send('i'); t.read(.1); self.assertNotIn('Propriedades', '\n'.join(t.screen.display))
            t.send('\t\n'); self.wait(t, 'Propriedades')
            t.send('\x1b[19~'); t.read(.1)
            self.assertNotIn('Propriedades', '\n'.join(t.screen.display))
            t.send('\x1b[19~'); self.wait(t, 'Propriedades')
            t.send('\x1b[<65;65;12M'); t.read(.1)
            self.assertNotIn('Nome', '\n'.join(t.screen.display))
            t.resize(14, 44); self.wait(t, 'Propriedades')
            button = self.button_position(t)
            t.click(*button); t.read(.1)
            self.assertNotIn('Propriedades', '\n'.join(t.screen.display))
            self.assertEqual(self.button_position(t), button)
            t.click(*button); self.wait(t, 'Propriedades')
            t.send('\x1b[6~'); t.send('\x1b[5~')
            t.resize(30, 100); self.wait(t, 'Propriedades')
            button = self.button_position(t)
            t.click(*button); t.read(.1)
            self.assertNotIn('Propriedades', '\n'.join(t.screen.display))
            t.click(*button); self.wait(t, 'Propriedades')
            t.click(8, 4); self.wait(t, 'Modal fechado')
            self.assertIn('Canais acionados: 0', '\n'.join(t.screen.display))
            t.send('\x1b[18~'); self.wait(t, 'Modal aberto')
            self.assertNotIn('Propriedades', '\n'.join(t.screen.display))
            t.send('\x1b'); self.wait(t, 'Modal fechado')
        finally:
            self.assertTrue(t.close())
            self.assertEqual(t.process.returncode, 0)
            for diagnostic in (b'AddressSanitizer', b'runtime error:'): self.assertNotIn(diagnostic, t.output)
if __name__ == '__main__': unittest.main()
