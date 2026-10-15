"""Cache identities, concurrent HTTP barriers, budgets and monotonic expiry.
Synthetic backend evidence; never uses a real account.
"""
import base64
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from history_mock import HistoryMock

BINARY = sys.argv.pop(1)
FFMPEG = sys.argv.pop(1)
# Tiny valid PNG, decoded by the same FFmpeg pipeline as the application.
PNG = base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aX1cAAAAASUVORK5CYII=')

class Cache(unittest.TestCase):
    def test_message_avatar_local_file_and_unicode_share_the_same_pipeline(self):
        self.run_unified_scenario('unified')

    def test_avatars_reuse_pixels_after_idle_without_ignoring_memory_budget(self):
        self.run_unified_scenario('avatars')

    def run_unified_scenario(self, mode):
        with HistoryMock(count=1) as server, tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'source.png'; path.write_bytes(PNG)
            original = server.route
            def route(method, route_path, body):
                if '/attachments/' in route_path or '/webhooks/' in route_path:
                    return 200, PNG, None, None, 'image/png'
                return original(method, route_path, body)
            server.api_handler = route
            result = subprocess.run([BINARY, server.url, mode],
                                    env=dict(os.environ, TMPDIR=directory, PAPO_TEST_LOCAL_IMAGE=str(path)),
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = json.loads(result.stdout)
            self.assertEqual(report['objects'], 2)  # One PNG object and one generated Unicode object.
            self.assertEqual(report['decodes'], 1)
            self.assertEqual(report['encodes'], 2)
            self.assertTrue(report['shared'])
            self.assertEqual(report['avatars_retained'], mode == 'avatars')
            self.assertEqual(list(Path(directory).iterdir()), [path])
            self.assertTrue(all(not r[2] for r in server.requests if '/webhooks/' in r[1]))

    def test_batch_coalesces_sizes_before_starting_one_decode(self):
        with HistoryMock(count=1) as server, tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'source.png'
            subprocess.run([FFMPEG, '-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=1000x500',
                            '-frames:v', '1', '-threads', '1', '-y', str(path)], check=True, capture_output=True)
            result = subprocess.run([BINARY, server.url, 'batch'],
                                    env=dict(os.environ, TMPDIR=directory, PAPO_TEST_LOCAL_IMAGE=str(path)),
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = json.loads(result.stdout)
            self.assertTrue(report['largest_size_shared'])
            self.assertEqual(report['decodes'], 1)
            self.assertEqual(report['encodes'], 2)
            self.assertEqual(list(Path(directory).iterdir()), [path])

    def test_resize_upgrades_shared_pixels_without_download_and_cancels_safely(self):
        with tempfile.TemporaryDirectory() as assets:
            path = Path(assets) / 'large.png'
            subprocess.run([FFMPEG, '-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=1000x500',
                            '-frames:v', '1', '-threads', '1', '-y', str(path)], check=True, capture_output=True)
            content = path.read_bytes()
            for mode in ('resize', 'cancel-upgrade'):
                with self.subTest(mode=mode), HistoryMock(count=1) as server, tempfile.TemporaryDirectory() as tmp:
                    original = server.route
                    def route(method, path, body):
                        if '/attachments/' in path:
                            return 200, content, None, None, 'image/png'
                        return original(method, path, body)
                    server.api_handler = route
                    result = subprocess.run([BINARY, server.url, mode], env=dict(os.environ, TMPDIR=tmp),
                                            capture_output=True, text=True, timeout=20)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual(sum('/attachments/' in r[1] for r in server.requests), 2)
                    self.assertEqual(list(Path(tmp).iterdir()), [])
                    print(result.stdout.strip())

    def test_concurrent_completions_share_objects_and_variants(self):
        self.run_identity_scenario('dedup')

    def test_same_revision_invalidation_fetches_again_and_reuses_identical_pixels(self):
        self.run_identity_scenario('invalidate')

    def run_identity_scenario(self, mode):
        with HistoryMock(count=1) as server, tempfile.TemporaryDirectory() as tmp:
            barrier = threading.Barrier(4)
            lock = threading.Lock()
            active = peak = requests = 0
            original = server.route
            def route(method, path, body):
                nonlocal active, peak, requests
                if '/attachments/' in path or '/embeds/' in path:
                    with lock:
                        active += 1
                        peak = max(peak, active)
                        requests += 1
                        synchronize = requests <= 4
                    if synchronize:
                        barrier.wait(timeout=5)
                        time.sleep(.08)
                    with lock: active -= 1
                    if '/embeds/' in path:
                        return 200, {'id': path.rsplit('/', 1)[1], 'image_data': base64.b64encode(PNG).decode(),
                                     'author_image_data': base64.b64encode(PNG).decode()}
                    return 200, PNG, None, None, 'image/png'
                return original(method, path, body)
            server.api_handler = route
            result = subprocess.run([BINARY, server.url, mode], env=dict(os.environ, TMPDIR=tmp),
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(peak, 4)
            self.assertEqual(requests, 6 if mode == 'invalidate' else 5)
            report = json.loads(result.stdout)
            self.assertEqual(report['objects'], 1)
            self.assertEqual(report['decodes'], 1)
            self.assertEqual(report['encodes'], 1)
            self.assertLess(report['first_ms'], 2000)
            self.assertEqual(list(Path(tmp).iterdir()), [])
            print(json.dumps(dict(report, concurrent_downloads=peak)))

    def test_real_decoded_gifs_pressure_and_repeated_expiry(self):
        with tempfile.TemporaryDirectory() as tmp, HistoryMock(count=1) as server:
            gif = Path(tmp) / 'pressure.gif'
            subprocess.run([FFMPEG, '-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=640x360:rate=10:duration=4.8',
                            '-frames:v', '48', '-threads', '1', '-y', str(gif)], check=True, capture_output=True)
            data = gif.read_bytes()
            self.assertTrue(data.endswith(b';'))
            original = server.route
            def route(method, path, body):
                if '/attachments/' in path:
                    # A valid GIF comment changes the original SHA-256 without
                    # duplicating fixture pixels or inventing response fields.
                    comment = path.rsplit('/', 1)[1].encode()
                    content = data[:-1] + b'!\xfe' + bytes([len(comment)]) + comment + b'\0;'
                    return 200, content, None, None, 'image/gif'
                return original(method, path, body)
            server.api_handler = route
            files = Path(tmp) / 'files'; files.mkdir()
            result = subprocess.run([BINARY, server.url, 'pressure'], env=dict(os.environ, TMPDIR=str(files)),
                                    capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr[-4000:])
            report = json.loads(result.stdout)
            self.assertLessEqual(report['peak_accounted_bytes'], 256 * 1024 * 1024)
            self.assertGreater(report['peak_accounted_bytes'], 240 * 1024 * 1024)
            if os.confstr('CS_GNU_LIBC_VERSION'):
                self.assertGreaterEqual(report['trim_attempts'], 3)
            self.assertEqual(list(files.iterdir()), [])
            print(json.dumps(report))

    def test_cancel_and_logout_remove_temporary_files_and_references(self):
        for mode in ('cancel', 'logout', 'cancel-ingest', 'cancel-decode', 'cancel-encode'):
            with self.subTest(mode=mode), HistoryMock(count=1) as server, tempfile.TemporaryDirectory() as tmp:
                original = server.route
                def route(method, path, body):
                    if '/attachments/' in path or '/embeds/' in path:
                        server.stopping.wait(.15)
                        if '/embeds/' in path:
                            return 200, {'id': path.rsplit('/', 1)[1], 'image_data': base64.b64encode(PNG).decode(), 'author_image_data': base64.b64encode(PNG).decode()}
                        return 200, PNG, None, None, 'image/png'
                    return original(method, path, body)
                server.api_handler = route
                result = subprocess.run([BINARY, server.url, mode], env=dict(os.environ, TMPDIR=tmp),
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(list(Path(tmp).iterdir()), [])

if __name__ == '__main__': unittest.main()
