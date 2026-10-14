"""Synthetic authenticated binary transport, not backend observations."""
import os
import stat
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from mock_server import MockServer
BINARY = sys.argv.pop(1)
ID = '00000000-0000-4000-8000-000000000901'

class MediaTests(unittest.TestCase):
    def probe(self, server, mode='image', env=None):
        with tempfile.TemporaryDirectory(prefix='papo-media-test-') as directory:
            result = subprocess.run([BINARY, server.url, mode], capture_output=True, text=True,
                                    env=dict(os.environ, TMPDIR=directory, **(env or {})), timeout=15)
            self.assertEqual(result.returncode, 0, 'Media probe failed without recording credentials')
            self.assertEqual(list(Path(directory).iterdir()), [], 'Temporary media was not removed')
            self.assertNotIn('Synthetic!123', result.stdout + result.stderr)
            return result.stdout

    def server(self, status=200, mime='image/png', body=b'fixture', delay=0):
        server = MockServer()
        def route(method, path, _body):
            if '/attachments/' not in path and '/embeds/' not in path: return None
            if delay: server.stopping.wait(delay)
            return status, body if status == 200 else None, None, '1' if status == 429 else None, mime
        server.api_handler = route
        return server

    def test_authenticated_private_file_and_independent_json_slot(self):
        with self.server(delay=.15) as server:
            output = self.probe(server)
            self.assertIn('ready=1', output)
            self.assertIn('private=1', output)
            self.assertIn('independent=1', output)
            calls = [r for r in server.requests if '/attachments/' in r[1]]
            self.assertEqual(len(calls), 1)
            self.assertEqual(calls[0][1], '/papo/attachments/' + ID)
            self.assertTrue(calls[0][2] and 'Auth=' in calls[0][2])

    def test_failures_are_bounded_and_never_retried(self):
        for status in (401, 403, 404, 429, 500, 502):
            with self.subTest(status=status), self.server(status=status) as server:
                output = self.probe(server)
                self.assertIn('failed=1', output)
                self.assertIn(f'status={status}', output)
                self.assertEqual(sum('/attachments/' in r[1] for r in server.requests), 1)

    def test_wrong_mime_and_size(self):
        with self.server(mime='text/html') as server:
            self.assertIn('failed=1', self.probe(server))
        with self.server(body=b'x' * (16 * 1024 * 1024 + 1)) as server:
            self.assertIn('failed=1', self.probe(server))

    def test_cancellation_logout_and_global_budget(self):
        for mode in ('cancel', 'logout', 'budget'):
            with self.subTest(mode=mode), self.server(delay=.2) as server:
                output = self.probe(server, mode)
                self.assertIn('discarded=1' if mode == 'logout' else 'cancelled=1', output)
                if mode == 'budget': self.assertFalse(any('/attachments/' in r[1] for r in server.requests))

    def test_uuid_rejected_before_http(self):
        with self.server() as server:
            self.assertIn('rejected=1', self.probe(server, 'invalid'))
            self.assertFalse(any('/attachments/' in r[1] for r in server.requests))

    def test_video_and_embed_endpoints(self):
        with self.server(mime='video/mp4') as server:
            self.assertIn('ready=1', self.probe(server, 'video'))
            self.assertTrue(any(r[1].endswith('/embeds/' + ID + '/video') for r in server.requests))
        with self.server(mime='application/json', body=b'{"image_data":null}') as server:
            self.assertIn('ready=1', self.probe(server, 'embed'))

if __name__ == '__main__': unittest.main()
