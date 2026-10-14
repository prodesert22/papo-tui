"""Cards, authenticated media and libmpv through a PTY Sixel responder.

The responder verifies emitted protocol bytes and screen controls, not an
actual emulator's pixel composition. Actual terminal evidence is separate.
"""
import base64
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
FFMPEG = sys.argv.pop(2)
from history_mock import HistoryMock, uuid
from test_terminal import Terminal, TerminalStream
import test_history_terminal as history_tests

class GraphicsStream:
    def __init__(self, screen):
        self.stream = TerminalStream(screen)
        self.pending = b''
        self.frames = []
    def feed(self, data):
        self.pending += data
        while self.pending:
            start = self.pending.find(b'\x1bP')
            if start < 0:
                size = len(self.pending) - (1 if self.pending.endswith(b'\x1b') else 0)
                self.stream.feed(self.pending[:size]); self.pending = self.pending[size:]; return
            if start:
                self.stream.feed(self.pending[:start]); self.pending = self.pending[start:]
            end = self.pending.find(b'\x1b\\', 2)
            if end < 0: return
            self.frames.append(self.pending[:end + 2]); self.pending = self.pending[end + 2:]

class MediaTerminal(unittest.TestCase):
    wait = history_tests.HistoryTerminal.wait
    @classmethod
    def setUpClass(cls):
        cls.assets = tempfile.TemporaryDirectory(prefix='papo-media-fixtures-')
        cls.images = {}
        for ext in ('png', 'jpg', 'webp', 'gif'):
            path = Path(cls.assets.name) / ('image.' + ext)
            subprocess.run([FFMPEG, '-v', 'error', '-f', 'lavfi', '-i', 'color=c=red:s=16x12',
                            '-frames:v', '1', '-threads', '1', '-y', str(path)], check=True, capture_output=True)
            cls.images[ext] = path.read_bytes()
        path = Path(cls.assets.name) / 'video.mp4'
        subprocess.run([FFMPEG, '-v', 'error', '-f', 'lavfi', '-i', 'color=c=blue:s=32x24:r=10',
                        '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=44100', '-t', '3',
                        '-c:v', 'mpeg4', '-c:a', 'aac', '-threads', '1', '-y', str(path)], check=True, capture_output=True)
        cls.video = path.read_bytes()
    @classmethod
    def tearDownClass(cls): cls.assets.cleanup()
    def open(self, server, sixel=True, environment=None):
        t = Terminal(environment=environment)
        if not hasattr(self, "terminals"): self.terminals = []
        self.terminals.append(t)
        stream = GraphicsStream(t.screen); stream.feed(bytes(t.output)); t.stream = stream
        if sixel:
            t.send('\x1b[?62;4;22c\x1b[6;16;8t')
        t.send(server.url + '\tjosé\tSynthetic!123\n')
        self.wait(t, 'geral'); t.send('\x1bOB\x1bOB\n'); self.wait(t, 'Editor')
        return t
    def tearDown(self):
        for terminal in getattr(self, 'terminals', []):
            self.assertEqual(terminal.process.returncode, 0, 'Client exited with an error')
            for diagnostic in (b'AddressSanitizer', b'LeakSanitizer', b'runtime error:'):
                self.assertNotIn(diagnostic, terminal.output, 'Sanitizer diagnostic in client output')
    def serve(self, content, mime, *, embed=False, slow=False):
        s = HistoryMock(count=1); parent = s.route
        s.messages[0]['attachments'] = [] if embed else [{'id': uuid(901), 'mime_type': mime,
            'original_file_name': 'Fixture', 'size_bytes': len(content), 'moderation_status': 'clean'}]
        def route(method, path, body):
            if '/attachments/' in path or '/embeds/' in path:
                if slow: s.stopping.wait(2)
                if embed:
                    return 200, {'id': uuid(902), 'image_data': base64.b64encode(content).decode(),
                                 'author_image_data': None}
                return 200, content, None, None, mime
            return parent(method, path, body)
        s.api_handler = route
        return s
    def wait_frames(self, terminal, count=1):
        deadline = time.monotonic() + 10
        while len(terminal.stream.frames) < count and time.monotonic() < deadline: terminal.read(.03)
        self.assertGreaterEqual(len(terminal.stream.frames), count)
    def test_supported_image_formats_emit_sixel_and_cleanup(self):
        for ext, mime in [('png', 'image/png'), ('jpg', 'image/jpeg'), ('webp', 'image/webp'), ('gif', 'image/gif')]:
            with self.subTest(ext=ext), tempfile.TemporaryDirectory() as directory, self.serve(self.images[ext], mime) as server:
                t = self.open(server, environment={'TMPDIR': directory})
                try:
                    self.wait_frames(t)
                    self.assertTrue(any(r[1].endswith('/attachments/' + uuid(901)) for r in server.requests))
                    self.assertNotIn('?62;4;22c', '\n'.join(t.screen.display))
                    self.assertNotIn(b'Synthetic!123', t.output)
                    t.send('Rascunho'); cursor = (t.screen.cursor.y, t.screen.cursor.x)
                    t.send('\x1b[<64;40;12M'); self.assertEqual((t.screen.cursor.y, t.screen.cursor.x), cursor)
                finally: self.assertTrue(t.close())
                self.assertEqual(list(Path(directory).iterdir()), [])
    def test_no_sixel_keeps_metadata_without_downloading(self):
        with self.serve(self.images['png'], 'image/png') as server:
            t = self.open(server, sixel=False)
            try:
                self.wait(t, 'Fixture'); t.read(.2)
                self.assertFalse(any('/attachments/' in r[1] for r in server.requests))
                t.send('\x1b[18~'); self.wait(t, 'suporte a Sixel')
                t.send('\x1b'); self.wait(t, 'Editor')
            finally: self.assertTrue(t.close())
    def test_embed_thumbnail_base64_and_unavailable_extra_image(self):
        with self.serve(self.images['png'], 'image/png', embed=True) as server:
            server.messages[0]['embeds'] = [{'id': uuid(902), 'title': 'Embed fixture',
                'thumbnail': {'mime_type': 'image/png'}, 'image': {'mime_type': 'image/png'}, 'footer': {'text': 'Footer'}}]
            t = self.open(server)
            try:
                self.wait_frames(t); self.wait(t, 'indisponível no backend')
                self.assertEqual(sum('/embeds/' in r[1] for r in server.requests), 1)
            finally: self.assertTrue(t.close())
    def test_corrupt_image_and_cancelled_video_leave_no_files(self):
        with tempfile.TemporaryDirectory() as directory, self.serve(b'not an image', 'image/png') as server:
            t = self.open(server, environment={'TMPDIR': directory})
            try: self.wait(t, 'Imagem inválida')
            finally: self.assertTrue(t.close())
            self.assertEqual(list(Path(directory).iterdir()), [])
        with tempfile.TemporaryDirectory() as directory, self.serve(self.video, 'video/mp4', slow=True) as server:
            t = self.open(server, environment={'TMPDIR': directory})
            try:
                self.wait(t, 'Vídeo'); t.send('\x1b[18~'); self.wait(t, 'Carregando mídia')
                t.send('\x1b'); self.wait(t, 'Editor'); t.send('Preservado'); self.wait(t, 'Preservado')
            finally: self.assertTrue(t.close())
            self.assertEqual(list(Path(directory).iterdir()), [])
    def test_channel_switch_cancels_late_media(self):
        with tempfile.TemporaryDirectory() as directory, self.serve(self.images['png'], 'image/png', slow=True) as server:
            t = self.open(server, environment={'TMPDIR': directory})
            try:
                self.wait(t, 'Carregando imagem')
                t.click(8, 4); self.wait(t, 'Conteúdo do outro canal'); t.read(2.2)
                self.assertEqual(t.stream.frames, [])
                self.assertNotIn('Fixture', '\n'.join(t.screen.display))
            finally: self.assertTrue(t.close())
            self.assertEqual(list(Path(directory).iterdir()), [])
    def test_video_modal_frames_pause_seek_resize_and_return(self):
        with tempfile.TemporaryDirectory() as directory, self.serve(self.video, 'video/mp4') as server:
            t = self.open(server, environment={'TMPDIR': directory})
            try:
                self.wait(t, 'Vídeo'); t.send('Rascunho'); t.send('\x1b'); t.send('\x1b[18~')
                self.wait_frames(t, 2); self.wait(t, 'Reproduzindo')
                t.send(' '); self.wait(t, 'Pausado')
                t.send('\x1bOC\x1bOA'); t.send('m')
                before_resize = len(t.stream.frames)
                t.resize(28, 100); self.wait_frames(t, before_resize + 1); self.wait(t, "Pausado")
                t.send('\x1b'); self.wait(t, 'Rascunho')
                self.assertFalse(any(r[0] != 'GET' for r in server.requests if '/attachments/' in r[1]))
            finally: self.assertTrue(t.close())
            self.assertEqual(list(Path(directory).iterdir()), [])
    def test_video_end_keeps_last_frame_and_can_seek_without_new_http(self):
        with self.serve(self.video, 'video/mp4') as server:
            t = self.open(server)
            try:
                t.send('\x1b[18~'); self.wait_frames(t, 2)
                self.wait(t, 'Pausado')
                self.assertIn('3 / 3 s', '\n'.join(t.screen.display))
                requests = sum('/attachments/' in r[1] for r in server.requests)
                t.send('\x1bOD'); t.send(' '); self.wait(t, 'Reproduzindo')
                self.assertEqual(sum('/attachments/' in r[1] for r in server.requests), requests)
            finally: self.assertTrue(t.close())
    def test_cards_inline_fields_and_resize(self):
        with HistoryMock(count=1) as server:
            server.messages[0]['embeds'] = [{'title': 'Card', 'author': {'name': 'Author'},
                'fields': [{'name': str(i), 'value': 'Value' + str(i), 'inline': True} for i in range(3)], 'footer': {'text': 'Footer'}}]
            t = self.open(server, sixel=False)
            try:
                t.resize(35, 120); t.send('\x1bOH'); self.wait(t, 'Value0')
                self.assertTrue(any('Value0' in line and 'Value1' in line and 'Value2' in line for line in t.screen.display))
                t.resize(35, 44); self.wait(t, 'Value0')
                self.assertFalse(any('Value0' in line and 'Value1' in line for line in t.screen.display))
                self.wait(t, 'Rodapé: Footer')
            finally: self.assertTrue(t.close())
if __name__ == '__main__': unittest.main()
