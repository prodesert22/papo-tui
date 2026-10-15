"""Lazy dependency loading and failure handling; synthetic HTTP and PTY only."""
import locale
from pathlib import Path
import shutil
import signal
import sys
import tempfile
import unittest

# test_media_terminal consumes the first two arguments when imported.
BINARY, FFMPEG, FAKE, IMAGE, VIDEO = map(Path, sys.argv[1:6])
sys.argv = [sys.argv[0], str(BINARY), str(FFMPEG), *sys.argv[6:]]
import test_media_terminal as media
import test_terminal
from history_mock import HistoryMock


class MediaBackends(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        media.MediaTerminal.setUpClass()

    @classmethod
    def tearDownClass(cls):
        media.MediaTerminal.tearDownClass()

    def setUp(self):
        self.previous = test_terminal.BINARY
        test_terminal.BINARY = str(BINARY.resolve())
        self.helper = media.MediaTerminal()

    def tearDown(self):
        self.helper.doCleanups()
        self.helper.tearDown()
        test_terminal.BINARY = self.previous

    def maps(self, terminal):
        return Path(f'/proc/{terminal.process.pid}/maps').read_text()

    def assert_text_only(self, terminal):
        maps = self.maps(terminal)
        for name in ('libmpv.', 'libavcodec.', 'libavformat.', 'libavutil.', IMAGE.name, VIDEO.name):
            self.assertNotIn(name, maps)

    def bundle(self, directory, image=None, video=None):
        root = Path(directory)
        executable = root / 'papo-tui'
        shutil.copy2(BINARY, executable)
        (root / 'papo-backends').mkdir()
        for source, name in ((image, IMAGE.name), (video, VIDEO.name)):
            if source:
                shutil.copy2(source, root / 'papo-backends' / name)
        test_terminal.BINARY = str(executable)

    def test_login_and_text_chat_do_not_load_multimedia_libraries(self):
        with tempfile.TemporaryDirectory() as tmp, HistoryMock(count=100) as server:
            self.bundle(tmp)
            terminal = test_terminal.Terminal()
            try:
                self.assert_text_only(terminal)
            finally:
                self.assertTrue(terminal.close())
            terminal = self.helper.open(server, sixel=False)
            try:
                self.assert_text_only(terminal)
                terminal.send('draft without codecs')
                self.assertIn('draft without codecs', '\n'.join(terminal.screen.display))
            finally:
                self.assertTrue(terminal.close())

    def test_first_image_loads_image_codec_and_preserves_video_dependency_for_later(self):
        with self.helper.serve(media.MediaTerminal.images['png'], 'image/png') as server:
            terminal = self.helper.open(server, protocol='kitty')
            try:
                self.helper.wait_frames(terminal)
                maps = self.maps(terminal)
                self.assertIn(IMAGE.name, maps)
                self.assertIn('libavcodec.', maps)
                self.assertNotIn(VIDEO.name, maps)
                self.assertNotIn('libmpv.', maps)
            finally:
                self.assertTrue(terminal.close())

    def test_abort_in_image_worker_restores_terminal(self):
        with tempfile.TemporaryDirectory() as tmp, self.helper.serve(media.MediaTerminal.images['png'], 'image/png') as server:
            self.bundle(tmp, FAKE)
            gate = Path(tmp) / 'abort-worker'
            terminal = self.helper.open(server, protocol='kitty', environment={'PAPO_TEST_BACKEND_CRASH_GATE': str(gate)})
            # This child is expected to abort; the ordinary helper requires a
            # zero status and remains active for all other media tests.
            self.helper.terminals.remove(terminal)
            try:
                gate.touch()
                terminal.process.wait(timeout=10)
                self.assertEqual(terminal.process.returncode, -signal.SIGABRT)
            finally:
                self.assertTrue(terminal.close())
            for reset in (b'\x1b[?2026l', b'\x1b[?1006l', b'\x1b[?2004l', b'\x1b[?1049l'):
                self.assertIn(reset, terminal.output)

    def test_first_video_loads_player_and_copied_bundle_works(self):
        with tempfile.TemporaryDirectory() as tmp, self.helper.serve(media.MediaTerminal.video, 'video/mp4') as server:
            self.bundle(tmp, IMAGE, VIDEO)
            terminal = self.helper.open(server, protocol='kitty')
            try:
                self.assert_text_only(terminal)
                terminal.send('Draft\x1b\x1b[18~')
                self.helper.wait_frames(terminal, 2)
                maps = self.maps(terminal)
                self.assertIn(VIDEO.name, maps)
                self.assertIn('libmpv.', maps)
                self.assertNotIn(IMAGE.name, maps)
            finally:
                self.assertTrue(terminal.close())

    def test_video_plays_with_comma_decimal_locale_and_utf8_text(self):
        previous = locale.setlocale(locale.LC_NUMERIC)
        selected = None
        try:
            for candidate in ("pt_BR.UTF-8", "de_DE.UTF-8", "fr_FR.UTF-8"):
                try:
                    locale.setlocale(locale.LC_NUMERIC, candidate)
                except locale.Error:
                    continue
                if locale.localeconv()["decimal_point"] == ",":
                    selected = candidate
                    break
        finally:
            locale.setlocale(locale.LC_NUMERIC, previous)
        if selected is None:
            self.skipTest("Requires a UTF-8 locale with a comma decimal separator")

        with self.helper.serve(media.MediaTerminal.video, "video/mp4") as server:
            terminal = self.helper.open(
                server, protocol="kitty", environment={"LANG": selected, "LC_ALL": selected}
            )
            try:
                terminal.send("Ação ç界\x1b\x1b[18~")
                self.helper.wait_frames(terminal, 2)
                self.helper.wait(terminal, "Reproduzindo")
                terminal.send(" ")
                self.helper.wait(terminal, "Pausado")
                self.assertNotIn(b"non-C locale", terminal.output)
                terminal.send("\x1b")
                self.assertIn("Ação ç界", "\n".join(terminal.screen.display))
            finally:
                self.assertTrue(terminal.close())

    def test_missing_or_incompatible_image_module_shows_error_and_preserves_draft(self):
        for source in (None, FAKE):
            with self.subTest(module=source), tempfile.TemporaryDirectory() as tmp, \
                    self.helper.serve(media.MediaTerminal.images['png'], 'image/png') as server:
                self.bundle(tmp, source)
                terminal = self.helper.open(server, protocol='kitty')
                try:
                    terminal.send('draft preserved\x1b\x1b[18~')
                    self.helper.wait(terminal, 'Módulo de imagens ausente')
                    terminal.send('\x1b')
                    self.assertIn('draft preserved', '\n'.join(terminal.screen.display))
                    self.assertNotIn('libavcodec.', self.maps(terminal))
                finally:
                    self.assertTrue(terminal.close())

    def test_missing_or_incompatible_video_module_shows_error_and_preserves_draft(self):
        for source in (None, FAKE):
            with self.subTest(module=source), tempfile.TemporaryDirectory() as tmp, \
                    self.helper.serve(media.MediaTerminal.video, 'video/mp4') as server:
                self.bundle(tmp, video=source)
                terminal = self.helper.open(server, protocol='kitty')
                try:
                    terminal.send('draft preserved\x1b\x1b[18~')
                    self.helper.wait(terminal, 'Módulo de vídeo ausente')
                    terminal.send('\x1b')
                    self.assertIn('draft preserved', '\n'.join(terminal.screen.display))
                    self.assertNotIn('libmpv.', self.maps(terminal))
                finally:
                    self.assertTrue(terminal.close())


if __name__ == '__main__':
    unittest.main()
