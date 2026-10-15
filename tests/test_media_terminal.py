"""Cards, authenticated media and libmpv through a PTY Sixel responder.

The responder verifies emitted protocol bytes and screen controls, not an
actual emulator's pixel composition. Actual terminal evidence is separate.
"""
import base64
import json
import os
import re
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
        self.protocols = []
        self.kitty_frames = []
        self.live_ids = set()
        self.live_placements = set()
        self.deleted_ids = []
        self.placements = []
        self.placement_positions = []
        self.screen = screen
        self.synchronizations = 0
        self.kitty_header = None
        self.kitty_parts = []
    def feed(self, data):
        self.synchronizations += data.count(b"\x1b[?2026h")
        self.pending += data
        while self.pending:
            markers = (b'\x1bP', b'\x1b_G', b'\x1b]1337;')
            positions = [self.pending.find(marker) for marker in markers]
            start = min((position for position in positions if position >= 0), default=-1)
            if start < 0:
                keep = max((n for marker in markers for n in range(1, len(marker))
                            if self.pending.endswith(marker[:n])), default=0)
                size = len(self.pending) - keep
                self.stream.feed(self.pending[:size]); self.pending = self.pending[size:]; return
            if start:
                self.stream.feed(self.pending[:start]); self.pending = self.pending[start:]
            iterm = self.pending.startswith(b'\x1b]1337;')
            terminator = b'\a' if iterm else b'\x1b\\'
            end = self.pending.find(terminator, 2)
            if end < 0: return
            sequence = self.pending[:end + len(terminator)]
            self.pending = self.pending[end + len(terminator):]
            if iterm and not sequence.startswith(b'\x1b]1337;File='):
                # OSC 1337 also carries the Kitty focus marker for shortcuts.
                # Only File reports contain image pixels.
                self.stream.feed(sequence)
                continue
            if sequence.startswith(b'\x1b_G'):
                header, _, data = sequence[3:-2].partition(b';')
                values = dict(field.split(b'=', 1) for field in header.split(b',') if b'=' in field)
                if values.get(b'a') == b'd':
                    image_id = int(values[b'i']); self.deleted_ids.append(image_id)
                    if b'p' in values:
                        self.live_placements.discard((image_id, int(values[b'p'])))
                    else:
                        self.live_placements = {pair for pair in self.live_placements if pair[0] != image_id}
                    if not any(pair[0] == image_id for pair in self.live_placements): self.live_ids.discard(image_id)
                elif values.get(b'a') == b'p':
                    self.placements.append(values)
                    self.placement_positions.append((self.screen.cursor.y, self.screen.cursor.x))
                    self.live_ids.add(int(values[b'i']))
                    self.live_placements.add((int(values[b'i']), int(values[b'p'])))
                elif values.get(b'a') in (b'T', b't'):
                    self.kitty_header = values; self.kitty_parts = []
                elif self.kitty_header is not None:
                    if data: self.kitty_parts.append(data)
                    if values.get(b'm') == b'0':
                        pixels = base64.b64decode(b''.join(self.kitty_parts), validate=True)
                        self.kitty_frames.append((self.kitty_header, pixels))
                        if self.kitty_header[b'a'] == b'T': self.live_ids.add(int(self.kitty_header[b'i']))
                        self.frames.append(pixels); self.protocols.append('kitty'); self.kitty_header = None
            else:
                self.frames.append(sequence); self.protocols.append('iterm2' if iterm else 'sixel')

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
        path = Path(cls.assets.name) / 'tall.png'
        subprocess.run([FFMPEG, '-v', 'error', '-f', 'lavfi', '-i', 'color=c=red:s=16x96',
                        '-frames:v', '1', '-threads', '1', '-y', str(path)], check=True, capture_output=True)
        cls.tall_image = path.read_bytes()
        cls.large_images = {}
        for shape, size in (('landscape', '1000x500'), ('portrait', '1000x1500')):
            path = Path(cls.assets.name) / (shape + '.png')
            subprocess.run([FFMPEG, '-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=' + size,
                            '-frames:v', '1', '-threads', '1', '-y', str(path)], check=True, capture_output=True)
            cls.large_images[shape] = path.read_bytes()
        path = Path(cls.assets.name) / 'animated.gif'
        subprocess.run([FFMPEG, '-v', 'error', '-f', 'lavfi', '-i', 'color=red:s=16x12:d=0.2:r=5',
                        '-f', 'lavfi', '-i', 'color=blue:s=16x12:d=0.2:r=5',
                        '-filter_complex', '[0:v][1:v]concat=n=2:v=1:a=0', '-frames:v', '2',
                        '-loop', '0', '-threads', '1', '-y', str(path)], check=True, capture_output=True)
        cls.animated_gif = path.read_bytes()
        path = Path(cls.assets.name) / 'video.mp4'
        subprocess.run([FFMPEG, '-v', 'error', '-f', 'lavfi', '-i', 'color=c=blue:s=32x24:r=10',
                        '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=44100', '-t', '3',
                        '-c:v', 'mpeg4', '-c:a', 'aac', '-threads', '1', '-y', str(path)], check=True, capture_output=True)
        cls.video = path.read_bytes()
    @classmethod
    def tearDownClass(cls): cls.assets.cleanup()
    def open(self, server, sixel=True, environment=None, protocol=None):
        unset = ()
        if protocol == 'iterm2':
            environment = dict(environment or {}, TERM_PROGRAM='iTerm.app', LC_TERMINAL='iTerm2')
            unset = ('KITTY_PID', 'KITTY_WINDOW_ID', 'KONSOLE_VERSION', 'VTE_VERSION', 'GHOSTTY_BIN_DIR',
                     'WEZTERM_EXECUTABLE', 'TMUX', 'STY')
        t = Terminal(environment=environment, unset_environment=unset)
        if not hasattr(self, "terminals"): self.terminals = []
        self.terminals.append(t)
        self.addCleanup(lambda: t.close() if t.process.poll() is None else None)
        stream = GraphicsStream(t.screen); stream.feed(bytes(t.output)); t.stream = stream
        if protocol == 'kitty':
            query = re.search(rb'\x1b_Gi=(\d+),s=1,v=1,a=q', t.output)
            self.assertIsNotNone(query)
            t.send('\x1b_Gi=' + query[1].decode() + ';OK\x1b\\\x1b[?62;22c\x1b[6;16;8t')
        elif protocol == 'iterm2':
            t.send('\x1b[?62;22c\x1b[6;16;8t')
        elif sixel:
            t.send('\x1b[?62;4;22c\x1b[6;16;8t')
        t.send(server.url + '\tjosé\tSynthetic!123\n')
        self.wait(t, 'geral'); t.send('\x1bOB\x1bOB\n'); self.wait(t, 'Conversar em #geral')
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
    def raster_size(self, terminal):
        stream = terminal.stream
        if not stream.frames: return 0, 0
        if stream.protocols[-1] == 'kitty':
            header = stream.kitty_frames[-1][0]
            return int(header[b's']), int(header[b'v'])
        if stream.protocols[-1] == 'sixel':
            raster = re.search(rb'"\d+;\d+;(\d+);(\d+)', stream.frames[-1])
            self.assertIsNotNone(raster)
            return int(raster[1]), int(raster[2])
        payload = base64.b64decode(stream.frames[-1].split(b':', 1)[1][:-1], validate=True)
        offset = int.from_bytes(payload[4:8], 'little')
        count = int.from_bytes(payload[offset:offset + 2], 'little')
        tags = {int.from_bytes(payload[pos:pos + 2], 'little'): int.from_bytes(payload[pos + 8:pos + 12], 'little')
                for pos in range(offset + 2, offset + 2 + count * 12, 12)}
        return tags[256], tags[257]
    def wait_raster(self, terminal, predicate):
        deadline = time.monotonic() + 10
        while not predicate(*self.raster_size(terminal)) and time.monotonic() < deadline: terminal.read(.03)
        size = self.raster_size(terminal)
        self.assertTrue(predicate(*size), (size, '\n'.join(terminal.screen.display)))
        return size
    def test_large_images_follow_history_and_modal_viewports(self):
        for protocol in ('sixel', 'kitty', 'iterm2'):
            for shape, content in self.large_images.items():
                embed = shape == 'portrait'
                with self.subTest(protocol=protocol, shape=shape), self.serve(content, 'image/png', embed=embed) as server:
                    if embed:
                        server.messages[0]['embeds'] = [{'id': uuid(902), 'source_type': 'link',
                            'title': 'Link preview', 'thumbnail': {'mime_type': 'image/png'}}]
                    t = self.open(server, protocol=protocol)
                    try:
                        self.wait_frames(t)
                        t.resize(70, 220)
                        size = self.wait_raster(t, lambda w, h: w > 400 and h > 360)
                        if shape == 'landscape':
                            self.assertTrue(1000 <= size[0] < 1008 and 500 <= size[1] < 516, size)
                        else:
                            self.assertEqual(size, (480, 720))
                            self.assertAlmostEqual(size[1] / size[0], 1.5, delta=.05)
                        if protocol == 'kitty':
                            t.read(.1)
                            placement = t.stream.placements[-1]
                            self.assertEqual(placement[b'y'], b'0')
                            self.assertLessEqual(int(placement[b'h']), 720)
                            self.assertAlmostEqual(int(placement[b'h']) / int(placement[b'w']),
                                                   .5 if shape == 'landscape' else 1.5, delta=.01)
                        count = len(t.stream.frames)
                        placements = len(t.stream.placements)
                        t.send('\x1b[18~'); self.wait(t, '[Info]')
                        if protocol == 'kitty':
                            deadline = time.monotonic() + 10
                            while len(t.stream.placements) <= placements and time.monotonic() < deadline: t.read(.03)
                            self.assertGreater(len(t.stream.placements), placements)
                        else: self.wait_frames(t, count + 1)
                        modal_size = self.wait_raster(t, lambda w, h: h > 720 if shape == 'portrait' else w > 1008)
                        self.assertAlmostEqual(modal_size[1] / modal_size[0],
                                               .5 if shape == 'landscape' else 1.5, delta=.05)
                        t.send('i'); self.wait(t, 'Propriedades')
                        self.wait_raster(t, lambda w, h: h > 720 if shape == 'portrait' else w > 1008)
                        t.resize(28, 100)
                        self.wait_raster(t, lambda w, h: 0 < h <= 256)
                        t.send('\x1b'); self.wait(t, 'Conversar em #')
                        endpoint = '/embeds/' if embed else '/attachments/'
                        self.assertEqual(sum(endpoint in r[1] for r in server.requests), 1)
                    finally: self.assertTrue(t.close())
    def test_history_height_cap_with_partial_terminal_cells(self):
        with self.serve(self.large_images['portrait'], 'image/png') as server:
            t = self.open(server, protocol='kitty')
            try:
                self.wait_frames(t)
                t.resize(70, 220)
                t.send('\x1b[6;25;11t')
                self.wait_raster(t, lambda w, h: 720 <= h <= 750 and 480 <= w <= 495)
                t.read(.1)
                placement = t.stream.placements[-1]
                self.assertEqual((int(placement[b'w']), int(placement[b'h'])), (480, 720))
                self.assertEqual(placement[b'y'], b'0')
                # The extra raster pixels only pad the last terminal cell;
                # the Kitty placement must stop at exactly 720 actual pixels.
                self.assertGreater(self.raster_size(t)[1], 720)
            finally: self.assertTrue(t.close())
    def test_gif_plays_inline_at_native_size_without_attachment_metadata(self):
        for protocol in ('sixel', 'kitty', 'iterm2'):
            with self.subTest(protocol=protocol), tempfile.TemporaryDirectory() as directory, self.serve(self.animated_gif, 'image/gif') as server:
                server.messages[0]['attachments'][0]['thumbnail_id'] = uuid(999)
                t = self.open(server, protocol=protocol, environment={'TMPDIR': directory})
                try:
                    self.wait_frames(t, 4); t.read(.1)
                    display = '\n'.join(t.screen.display)
                    for metadata in ('Anexo:', 'Tipo: image/gif', 'Moderação:', '[Imagem / F7]', 'Fixture'):
                        self.assertNotIn(metadata, display)
                    self.assertEqual(sum('/attachments/' in r[1] for r in server.requests), 1)
                    self.assertTrue(any(r[1].endswith('/attachments/' + uuid(901)) for r in server.requests))
                    if protocol == 'kitty':
                        self.assertEqual(len({header[b'i'] for header, _ in t.stream.kitty_frames}), 1)
                        self.assertTrue(all(int(header[b's']) == 16 and int(header[b'v']) == 16 for header, _ in t.stream.kitty_frames))
                        # Require distinct red/blue frames without assuming
                        # full intensity in the graphic encoder's output.
                        self.assertTrue(any(pixels[0] > 128 and pixels[2] < 25 for _, pixels in t.stream.kitty_frames))
                        self.assertTrue(any(pixels[2] > 128 and pixels[0] < 25 for _, pixels in t.stream.kitty_frames))
                        self.assertTrue(all(p[b'c'] == b'0' and p[b'r'] == b'0' and p[b'h'] == b'12' for p in t.stream.placements))
                    t.click(8, 4); self.wait(t, 'Conteúdo do outro canal'); t.read(.15)
                    frames = len(t.stream.frames); t.read(.4)
                    self.assertEqual(len(t.stream.frames), frames)
                    self.assertFalse(t.stream.live_ids)
                finally: self.assertTrue(t.close())
                self.assertEqual(list(Path(directory).iterdir()), [])

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
                t.send('\x1b[18~'); self.wait(t, 'Este terminal não oferece')
                t.send('\x1b'); self.wait(t, 'Conversar em #')
            finally: self.assertTrue(t.close())
    def test_kitty_image_formats_use_graphics_pixels_and_delete_on_switch(self):
        for ext, mime in [('png', 'image/png'), ('jpg', 'image/jpeg'), ('webp', 'image/webp'), ('gif', 'image/gif')]:
            with self.subTest(ext=ext), self.serve(self.images[ext], mime) as server:
                t = self.open(server, protocol='kitty')
                try:
                    self.wait_frames(t)
                    self.assertEqual(set(t.stream.protocols), {'kitty'})
                    header, pixels = t.stream.kitty_frames[-1]
                    self.assertEqual(header[b'f'], b'32')
                    self.assertEqual(header[b'q'], b'2')
                    self.assertEqual(len(pixels), int(header[b's']) * int(header[b'v']) * 4)
                    self.assertGreater(pixels[0], 230)
                    self.assertLess(pixels[1], 25)
                    self.assertLess(pixels[2], 25)
                    self.assertNotIn('OK', '\n'.join(t.screen.display))
                    live = t.stream.live_ids.copy(); self.assertTrue(live)
                    t.click(8, 4); self.wait(t, 'Conteúdo do outro canal')
                    self.assertFalse(t.stream.live_ids)
                    self.assertTrue(live.issubset(t.stream.deleted_ids))
                finally: self.assertTrue(t.close())
    def test_kitty_video_pause_seek_resize_and_cleanup(self):
        with tempfile.TemporaryDirectory() as directory, self.serve(self.video, 'video/mp4') as server:
            t = self.open(server, protocol='kitty', environment={'TMPDIR': directory})
            try:
                t.send('Rascunho Kitty\x1b\x1b[18~')
                self.wait_frames(t, 2); self.wait(t, 'Reproduzindo')
                self.assertEqual(set(t.stream.protocols), {'kitty'})
                header, pixels = t.stream.kitty_frames[-1]
                self.assertEqual(len(pixels), int(header[b's']) * int(header[b'v']) * 4)
                center = (int(header[b'v']) // 2 * int(header[b's']) + int(header[b's']) // 2) * 4
                self.assertGreater(pixels[center + 2], 230)
                self.assertLess(pixels[center], 25)
                t.send(' '); self.wait(t, 'Pausado')
                t.send('\x1bOC\x1bOA'); t.send('m')
                self.assertEqual(len({header[b'i'] for header, _ in t.stream.kitty_frames}), 1)
                self.assertEqual(t.stream.deleted_ids, [])
                self.assertGreater(t.stream.synchronizations, 0)
                frames = len(t.stream.frames)
                t.resize(28, 100); self.wait_frames(t, frames + 1); self.wait(t, 'Pausado')
                self.assertLessEqual(len(t.stream.live_ids), 1)
                t.send('\x1b'); self.wait(t, 'Rascunho Kitty')
                self.assertFalse(t.stream.live_ids)
                self.assertEqual(sum('/attachments/' in r[1] for r in server.requests), 1)
            finally: self.assertTrue(t.close())
            self.assertEqual(list(Path(directory).iterdir()), [])
    def test_stationary_images_are_retained_across_focus_and_typing(self):
        for protocol in ('sixel', 'kitty', 'iterm2'):
            with self.subTest(protocol=protocol), self.serve(self.images['png'], 'image/png') as server:
                t = self.open(server, protocol=protocol)
                try:
                    self.wait_frames(t); t.read(.2)
                    frames = len(t.stream.frames)
                    deletes = len(t.stream.deleted_ids)
                    placements = len(t.stream.placements)
                    for key in ('draft', '\x1b', '\n', '\x1b', '\x1b', '\n'):
                        t.send(key); t.read(.08)
                        self.assertEqual(len(t.stream.frames), frames, 'Stationary picture was retransmitted')
                        self.assertEqual(len(t.stream.deleted_ids), deletes, 'Stationary picture was deleted')
                        self.assertEqual(len(t.stream.placements), placements, 'Stationary picture was replaced')
                    self.assertEqual(sum('/attachments/' in r[1] for r in server.requests), 1)
                finally: self.assertTrue(t.close())

    def test_identical_embed_update_invalidates_pixels_in_history_and_account(self):
        from realtime_mock import RealtimeMock
        from history_mock import CHANNEL
        with RealtimeMock(count=1) as server:
            embed = {'id': uuid(902), 'source_type': 'custom', 'title': 'Mutable preview',
                     'thumbnail': {'mime_type': 'image/png'}, 'fields': []}
            server.messages[0]['embeds'] = [embed]
            original = server.route
            content = self.images['png']
            calls = 0
            def route(method, path, body):
                nonlocal calls
                if '/embeds/' in path:
                    calls += 1
                    return 200, {'id': uuid(902), 'image_data': base64.b64encode(content).decode(), 'author_image_data': None}
                return original(method, path, body)
            server.api_handler = route
            t = self.open(server, protocol='kitty')
            try:
                self.wait_frames(t); t.resize(40, 100); t.read(.15)
                self.assertEqual(calls, 1)
                old_ids = {header[b'i'] for header, _ in t.stream.kitty_frames}
                content = self.images['webp']
                event = {'type': 'message_embeds_update', 'channel_id': CHANNEL,
                         'message_id': server.messages[0]['id'], 'embeds': [embed]}
                server.emit(event)
                deadline = time.monotonic() + 5
                while (calls < 2 or not ({header[b'i'] for header, _ in t.stream.kitty_frames} - old_ids)) and time.monotonic() < deadline:
                    t.read(.03)
                self.assertEqual(calls, 2)
                self.assertTrue({header[b'i'] for header, _ in t.stream.kitty_frames} - old_ids)
                self.assertEqual(sum('/messages' in r[1] for r in server.requests), 1)
                t.send('Preserved draft'); t.send('\x1b[17~'); self.wait(t, '[Canais / F6]')
                content = self.images['png']; server.emit(event); t.read(.2)
                t.send('\x1b[17~'); self.wait(t, 'geral'); t.send('\x1bOB\x1bOB\n')
                self.wait(t, 'Preserved draft')
                deadline = time.monotonic() + 5
                while calls < 3 and time.monotonic() < deadline: t.read(.03)
                self.assertEqual(calls, 3)
            finally: self.assertTrue(t.close())

    def test_identical_images_have_one_kitty_image_and_four_positions(self):
        for shared_id in (False, True):
            with self.subTest(shared_resource=shared_id), self.serve(self.images['png'], 'image/png') as server:
                original = server.messages[0]['attachments'][0]
                server.messages[0]['attachments'] = [dict(original, id=uuid(901 if shared_id else 901 + i)) for i in range(4)]
                t = self.open(server, protocol='kitty')
                try:
                    t.resize(64, 80)
                    deadline = time.monotonic() + 10
                    while len(t.stream.live_placements) < 4 and time.monotonic() < deadline: t.read(.03)
                    self.assertEqual(len(t.stream.live_placements), 4)
                    self.assertEqual(len(t.stream.live_ids), 1)
                    self.assertEqual(len({p[b'p'] for p in t.stream.placements}), 4)
                    self.assertEqual(sum('/attachments/' in r[1] for r in server.requests), 1 if shared_id else 4)
                    self.assertEqual(len({header[b'i'] for header, _ in t.stream.kitty_frames}), 1)
                    t.send('Shared pixels'); self.assertEqual(len(t.stream.live_placements), 4)
                    t.click(8, 4); self.wait(t, 'Conteúdo do outro canal'); t.read(.1)
                    self.assertFalse(t.stream.live_ids)
                finally: self.assertTrue(t.close())
    def test_image_gallery_wraps_with_gaps_and_keeps_click_targets(self):
        for embed in (False, True):
            with self.subTest(embed=embed), self.serve(self.images['png'], 'image/png', embed=embed) as server:
                if embed:
                    server.messages[0]['embeds'] = [{'id': uuid(901 + i), 'source_type': 'link',
                        'title': 'Preview ' + str(i + 1), 'thumbnail': {'mime_type': 'image/png'}} for i in range(4)]
                    parent = server.api_handler
                    def route(method, path, body):
                        if '/embeds/' in path:
                            return 200, {'id': path.rsplit('/', 1)[1],
                                'image_data': base64.b64encode(self.images['png']).decode(), 'author_image_data': None}
                        return parent(method, path, body)
                    server.api_handler = route
                else:
                    original = server.messages[0]['attachments'][0]
                    server.messages[0]['attachments'] = [dict(original, id=uuid(901 + i)) for i in range(4)]
                t = self.open(server, protocol='kitty')
                try:
                    t.resize(50, 150); t.send('\x1b[6;4;1t')
                    def slots():
                        live = {}
                        for position, p in zip(t.stream.placement_positions, t.stream.placements):
                            key = int(p[b'i']), int(p[b'p'])
                            if key in t.stream.live_placements: live[key] = (*position, int(p[b'w']), int(p[b'h']))
                        return sorted(live.values())
                    def wait_rows(rows):
                        deadline = time.monotonic() + 10
                        while time.monotonic() < deadline:
                            found = slots()
                            if len(found) == 4 and len({p[0] for p in found}) == rows and all(p[2:] == (16, 12) for p in found):
                                return found
                            t.read(.03)
                        self.fail('Gallery placements did not settle: ' + repr(slots()))
                    wide = wait_rows(1)
                    gap = 6 if embed else 2  # Cards retain two padding columns on either side.
                    self.assertTrue(all(wide[i + 1][1] - wide[i][1] == 16 + gap for i in range(3)))
                    t.resize(50, 80); narrow = wait_rows(2)
                    self.assertEqual(narrow[0][0], narrow[1][0])
                    self.assertEqual(narrow[2][0], narrow[3][0])
                    self.assertEqual(narrow[0][1], narrow[2][1])
                    self.assertEqual(narrow[2][0] - narrow[0][0], 8 if embed else 4)
                    self.assertEqual(len(t.stream.live_ids), 1)
                    endpoint = '/embeds/' if embed else '/attachments/'
                    self.assertEqual(sum(endpoint in r[1] for r in server.requests), 4)
                    t.click(narrow[0][0] + 1, narrow[0][1] + 16)
                    self.assertNotIn('[Info]', '\n'.join(t.screen.display))
                    t.click(narrow[1][0] + 1, narrow[1][1] + 8); self.wait(t, '[Info]')
                    t.send('i'); self.wait(t, 'Propriedades'); self.wait(t, uuid(902)[-6:])
                    t.send('\x1b'); self.wait(t, 'Conversar em #')
                    self.assertEqual(sum(endpoint in r[1] for r in server.requests), 4)
                finally: self.assertTrue(t.close())

    def test_kitty_scrolling_repositions_and_crops_without_retransmission(self):
        with self.serve(self.tall_image, 'image/png') as server:
            server.messages[0]['content'] = '\n'.join('Line %d' % n for n in range(30))
            t = self.open(server, protocol='kitty')
            try:
                self.wait_frames(t); t.read(.2)
                t.send('\x1b[5~')
                for _ in range(3): t.send('\x1bOB'); t.read(.08)
                self.wait_frames(t, 2); t.read(.15)
                frames = len(t.stream.frames)
                image_id = t.stream.placements[-1][b'i']
                deletes = len(t.stream.deleted_ids)
                initial = len(t.stream.placements)
                for _ in range(2): t.send('\x1bOB'); t.read(.08)
                self.assertGreater(len(t.stream.placements), initial)
                self.assertEqual(len(t.stream.frames), frames)
                self.assertEqual(len(t.stream.deleted_ids), deletes)
                self.assertEqual({p[b'i'] for p in t.stream.placements[initial:]}, {image_id})
                self.assertEqual(len({p[b'p'] for p in t.stream.placements}), 1)
                self.assertTrue(any(int(p[b'h']) < 96 for p in t.stream.placements))
                self.assertEqual(sum('/attachments/' in r[1] for r in server.requests), 1)
            finally: self.assertTrue(t.close())

    def test_resize_repaints_images_that_already_fit_at_original_size(self):
        for protocol in ('sixel', 'kitty', 'iterm2'):
            with self.subTest(protocol=protocol), self.serve(self.images['png'], 'image/png') as server:
                t = self.open(server, protocol=protocol)
                try:
                    self.wait_frames(t)
                    t.resize(35, 120); self.wait_frames(t, 2); t.read(.2)
                    frame = t.stream.frames[-1]
                    frames = len(t.stream.frames)
                    t.resize(35, 140); self.wait_frames(t, frames + 1)
                    self.assertEqual(t.stream.frames[-1], frame)
                    self.assertEqual(sum('/attachments/' in r[1] for r in server.requests), 1)
                finally: self.assertTrue(t.close())

    def test_cached_crops_scroll_without_a_loading_placeholder(self):
        for protocol in ('sixel', 'iterm2'):
            with self.subTest(protocol=protocol), self.serve(self.tall_image, 'image/png') as server:
                server.messages[0]['content'] = '\n'.join('Line %d' % n for n in range(30))
                t = self.open(server, protocol=protocol)
                try:
                    self.wait_frames(t); t.send('\x1b[5~')
                    for _ in range(5):
                        t.send('\x1bOB'); t.read(.08)
                        self.assertNotIn('Carregando imagem', '\n'.join(t.screen.display))
                    self.assertGreater(len(t.stream.frames), 2)
                    self.assertEqual(sum('/attachments/' in r[1] for r in server.requests), 1)
                finally: self.assertTrue(t.close())

    def test_modal_fits_small_embed_and_properties_reuses_download(self):
        with self.serve(self.images['png'], 'image/png', embed=True) as server:
            server.messages[0]['embeds'] = [{'id': uuid(902), 'source_type': 'link',
                'created_at': '2026-10-14T15:51:18Z', 'title': 'Link preview',
                'thumbnail': {'mime_type': 'image/png', 'size_bytes': len(self.images['png'])},
                'image': {'mime_type': 'image/png'}}]
            t = self.open(server, protocol='kitty')
            try:
                self.wait_frames(t); t.send('\x1b[18~'); self.wait(t, '[Info]')
                self.wait(t, "Imagem"); self.wait_raster(t, lambda w, h: w >= 232 and h >= 176); t.read(.1)
                # 80x24: viewer keeps channels at x<26, with outer margins
                # at (30,3). The image area spans x=33..72 and y=6..16.
                self.assertEqual(t.stream.placement_positions[-1], (6, 38))
                placement = t.stream.placements[-1]
                self.assertEqual((int(placement[b'w']), int(placement[b'h'])), (234, 176))
                self.assertIn('Canais', '\n'.join(t.screen.display))
                frames = len(t.stream.frames)
                image_id = t.stream.placements[-1][b'i']
                t.click(4, 33); self.wait(t, 'Propriedades'); t.read(.15)
                self.wait_frames(t, frames + 1); t.read(.1)
                self.assertEqual(t.stream.placement_positions[-1], (7, 33))
                placement = t.stream.placements[-1]
                self.assertEqual((int(placement[b'w']), int(placement[b'h'])), (168, 126))
                self.assertIn('image/png', '\n'.join(t.screen.display))
                self.assertIn(str(len(self.images['png'])) + ' bytes', '\n'.join(t.screen.display))
                frames = len(t.stream.frames)
                t.click(4, 33); t.read(.1)
                self.wait_frames(t, frames + 1); t.read(.05)
                self.assertNotIn('Propriedades', '\n'.join(t.screen.display))
                self.assertEqual(t.stream.placement_positions[-1], (6, 38))
                self.assertEqual(t.stream.placements[-1][b'i'], image_id)
                # Kitty deleted the inactive placement, so it needs another
                # upload of the same cached variant when its pane returns.
                self.assertEqual(len(t.stream.frames), frames + 1)
                t.click(8, 4); t.read(.2)
                self.assertNotIn('Esc: voltar', '\n'.join(t.screen.display))
                self.assertIn('# geral', '\n'.join(t.screen.display))
                self.assertEqual(sum('/embeds/' in r[1] for r in server.requests), 1)
                self.assertFalse(any('/channels/' + uuid(11) + '/messages' in r[1] for r in server.requests))
            finally: self.assertTrue(t.close())

    def test_kitty_rejected_probe_keeps_text_without_downloads(self):
        with self.serve(self.images['png'], 'image/png') as server:
            t = self.open(server, sixel=False)
            try:
                query = re.search(rb'\x1b_Gi=(\d+),s=1,v=1,a=q', t.output)
                self.assertIsNotNone(query)
                t.send('\x1b_Gi=' + query[1].decode() + ';ENOTSUP\x1b\\\x1b[6;16;8t')
                self.wait(t, 'Fixture'); t.read(.2)
                self.assertFalse(any('/attachments/' in r[1] for r in server.requests))
                self.assertNotIn('ENOTSUP', '\n'.join(t.screen.display))
                self.assertFalse(t.stream.frames)
            finally: self.assertTrue(t.close())
    def test_kitty_delayed_fragmented_reply_enables_existing_viewport(self):
        with self.serve(self.images['png'], 'image/png') as server:
            t = self.open(server, sixel=False)
            try:
                self.wait(t, 'Conversar em #')
                query = re.search(rb'\x1b_Gi=(\d+),s=1,v=1,a=q', t.output)
                self.assertIsNotNone(query)
                t.send('\x1b_Gi=' + query[1].decode())
                t.send(';OK\x1b\\')
                t.send('\x1b[6;16;8t')
                self.wait_frames(t)
                self.assertEqual(set(t.stream.protocols), {'kitty'})
                self.assertNotIn('OK', '\n'.join(t.screen.display))
                self.assertEqual(sum('/attachments/' in r[1] for r in server.requests), 1)
            finally: self.assertTrue(t.close())
    def test_iterm2_inline_pixels_and_video_controls(self):
        with self.serve(self.images['png'], 'image/png') as server:
            t = self.open(server, protocol='iterm2')
            try:
                self.wait_frames(t)
                self.assertEqual(set(t.stream.protocols), {'iterm2'})
                payload = base64.b64decode(t.stream.frames[-1].split(b':', 1)[1][:-1], validate=True)
                self.assertEqual(payload[:4], b'II*\0')
                first_ifd = int.from_bytes(payload[4:8], 'little')
                count = int.from_bytes(payload[first_ifd:first_ifd + 2], 'little')
                tags = {}
                for offset in range(first_ifd + 2, first_ifd + 2 + count * 12, 12):
                    tags[int.from_bytes(payload[offset:offset + 2], 'little')] = int.from_bytes(payload[offset + 8:offset + 12], 'little')
                self.assertLessEqual(tags[256], 640); self.assertLessEqual(tags[257], 360)
                self.assertGreater(payload[tags[273]], 230)
                t.send('\x1b[18~'); self.wait_frames(t, 2)
                t.send('\x1b'); self.wait(t, 'Conversar em #')
            finally: self.assertTrue(t.close())
        with self.serve(self.video, 'video/mp4') as server:
            t = self.open(server, protocol='iterm2')
            try:
                t.send('\x1b[18~'); self.wait_frames(t, 2); self.wait(t, 'Reproduzindo')
                t.send(' '); self.wait(t, 'Pausado')
                t.resize(28, 100); self.wait(t, 'Pausado')
                self.assertEqual(set(t.stream.protocols), {'iterm2'})
                t.send('\x1b'); self.wait(t, 'Conversar em #')
            finally: self.assertTrue(t.close())
    def test_channel_navigation_preserves_image_and_reopening_uses_cache(self):
        with self.serve(self.images['png'], 'image/png') as server:
            t = self.open(server)
            try:
                self.wait_frames(t)
                t.send('Rascunho com imagem\x1b')
                t.click(5, 4); t.read(.2)
                conversation = [row[26:] for row in t.screen.display[5:-3]]
                frames = len(t.stream.frames)
                requests = [r for r in server.requests if '/messages' in r[1] or '/attachments/' in r[1]]
                for key in ('\x1bOB', '\x1bOA', '\x1bOH', '\x1bOF', '\x1b[5~', '\x1b[6~'):
                    t.send(key)
                    self.assertEqual([row[26:] for row in t.screen.display[5:-3]], conversation)
                    self.assertEqual(len(t.stream.frames), frames, 'Navigation redrew the image')
                t.send('\x1bOH\x1bOB\n')
                self.wait(t, 'Rascunho com imagem')
                t.send('\x1b[18~'); self.wait(t, 'Imagem')
                self.wait_frames(t, frames + 1)
                self.assertEqual([r for r in server.requests if '/messages' in r[1] or '/attachments/' in r[1]], requests)
                t.send('\x1b'); self.wait(t, 'Rascunho com imagem')
            finally: self.assertTrue(t.close())
    def test_embed_author_and_thumbnail_share_header(self):
        with self.serve(self.images['png'], 'image/png', embed=True) as server:
            parent = server.api_handler
            def route(method, path, body):
                if '/embeds/' in path:
                    encoded = base64.b64encode(self.images['png']).decode()
                    return 200, {'id': uuid(902), 'image_data': encoded, 'author_image_data': encoded}
                return parent(method, path, body)
            server.api_handler = route
            server.messages[0]['content'] = None
            server.messages[0]['embeds'] = [{'id': uuid(902), 'source_type': 'custom',
                'author': {'name': 'Info', 'media': {'mime_type': 'image/png'}},
                'site_name': 'Info',
                'title': 'Example Title', 'url': 'https://example.com',
                'thumbnail': {'mime_type': 'image/png'}, 'footer': {'text': 'Example Footer'}}]
            t = self.open(server, protocol='kitty')
            try:
                t.resize(35, 120); t.send('\x1bOH')
                self.wait(t, 'Info'); self.wait_frames(t, 2); t.read(.15)
                sizes = {(int(header[b'c']), int(header[b'r'])) for header, _ in t.stream.kitty_frames}
                self.assertIn((4, 2), sizes)
                self.assertIn((12, 4), sizes)
                self.assertEqual(sum('/embeds/' in r[1] for r in server.requests), 1)
                display = '\n'.join(t.screen.display)
                self.assertEqual(display.count('Info'), 1)
                self.assertNotIn('Avatar do autor', display)
                self.assertNotIn('[Imagem / F7]', display)
                self.assertNotIn('https://example.com', display)
                self.assertNotIn('Sem conteúdo textual', display)
                self.assertNotIn('Rodapé:', display)
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
                t.send('\x1b'); self.wait(t, 'Conversar em #geral'); t.send('Preservado'); self.wait(t, 'Preservado')
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
            server.messages[0]['content'] = None
            server.messages[0]['embeds'] = [{'title': 'Card', 'color': '#00b0f4', 'author': {'name': 'Author'},
                'fields': [{'name': str(i), 'value': 'Value' + str(i), 'inline': True} for i in range(3)], 'footer': {'text': 'Footer'}}]
            t = self.open(server, sixel=False)
            try:
                t.resize(35, 120); t.send('\x1bOH'); self.wait(t, 'Value0')
                self.assertTrue(any('Value0' in line and 'Value1' in line and 'Value2' in line for line in t.screen.display))
                display = '\n'.join(t.screen.display)
                self.assertNotIn('Sem conteúdo textual', display)
                self.assertNotIn('Rodapé:', display)
                bar_y, bar_x = next((y, line.index('▎')) for y, line in enumerate(t.screen.display) if 'Card' in line)
                bar = t.screen.buffer[bar_y][bar_x]
                body = t.screen.buffer[bar_y][bar_x + 2]
                outside = t.screen.buffer[bar_y][bar_x - 1]
                self.assertEqual(bar.bg, body.bg)
                self.assertNotEqual(body.bg, outside.bg)
                self.assertNotEqual(bar.fg, body.fg)
                self.assertTrue(all(t.screen.buffer[y][bar_x].data == '▎'
                                    for y, line in enumerate(t.screen.display) if 'Author' in line or 'Footer' in line))
                for word in ('Author', 'Footer'):
                    y, x = next((y, line.index(word)) for y, line in enumerate(t.screen.display) if word in line)
                    self.assertFalse(t.screen.buffer[y][x].bold)
                    self.assertNotEqual(t.screen.buffer[y][x].fg, body.fg)
                t.resize(35, 44); self.wait(t, 'Value0')
                self.assertFalse(any('Value0' in line and 'Value1' in line for line in t.screen.display))
                self.wait(t, 'Footer')
                self.assertNotIn('Rodapé:', '\n'.join(t.screen.display))
            finally: self.assertTrue(t.close())
if __name__ == '__main__': unittest.main()
