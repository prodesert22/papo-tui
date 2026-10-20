"""Synthetic authenticated binary transport, not backend observations."""

from test_context import main as test_main

from test_context import argument

import os
import ssl
import subprocess
import tempfile
import unittest
from pathlib import Path
from mock_server import MockServer

BINARY = argument(0)
ID = "00000000-0000-4000-8000-000000000901"


class MediaTests(unittest.TestCase):
    def probe(self, server, mode="image", env=None):
        with tempfile.TemporaryDirectory(prefix="papo-media-test-") as directory:
            result = subprocess.run(
                [BINARY, server.url, mode],
                capture_output=True,
                text=True,
                env=dict(os.environ, TMPDIR=directory, **(env or {})),
                timeout=15,
            )
            self.assertEqual(
                result.returncode, 0, "Media probe failed without recording credentials"
            )
            self.assertEqual(list(Path(directory).iterdir()), [], "Temporary media was not removed")
            self.assertNotIn("Synthetic!123", result.stdout + result.stderr)
            return result.stdout

    def server(self, status=200, mime="image/png", body=b"fixture", delay=0):
        server = MockServer()

        def route(method, path, _body):
            if "/attachments/" not in path and "/embeds/" not in path and "/webhooks/" not in path:
                return None
            if delay:
                server.stopping.wait(delay)
            return (
                status,
                body if status == 200 else None,
                None,
                "1" if status == 429 else None,
                mime,
            )

        server.api_handler = route
        return server

    def test_authenticated_private_file_and_independent_json_slot(self):
        with self.server(delay=0.15) as server:
            output = self.probe(server)
            self.assertIn("ready=1", output)
            self.assertIn("private=1", output)
            self.assertIn("independent=1", output)
            calls = [r for r in server.requests if "/attachments/" in r[1]]
            self.assertEqual(len(calls), 1)
            self.assertEqual(calls[0][1], "/papo/attachments/" + ID)
            self.assertTrue(calls[0][2] and "Auth=" in calls[0][2])

    def test_failures_are_bounded_and_never_retried(self):
        for status in (401, 403, 404, 429, 500, 502):
            with self.subTest(status=status), self.server(status=status) as server:
                output = self.probe(server)
                self.assertIn("failed=1", output)
                self.assertIn(f"status={status}", output)
                self.assertEqual(sum("/attachments/" in r[1] for r in server.requests), 1)

    def test_wrong_mime_and_size(self):
        with self.server(mime="text/html") as server:
            self.assertIn("failed=1", self.probe(server))
        for mode, mime, limit in (
            ("image", "image/png", 32 * 1024 * 1024),
            ("avatar", "image/png", 16 * 1024 * 1024),
            ("video", "video/mp4", 256 * 1024 * 1024),
        ):
            for extra in (0, 1):
                with (
                    self.subTest(mode=mode, extra=extra),
                    self.server(mime=mime, body=b"x" * (limit + extra)) as server,
                ):
                    output = self.probe(server, mode)
                    self.assertIn("failed=1" if extra else "ready=1", output)
                    if not extra:
                        self.assertIn(f"bytes={limit} ", output)

    def test_cancellation_logout_and_global_budget(self):
        for mode in ("cancel", "logout", "budget"):
            with self.subTest(mode=mode), self.server(delay=0.2) as server:
                output = self.probe(server, mode)
                self.assertIn("discarded=1" if mode == "logout" else "cancelled=1", output)
                if mode == "budget":
                    self.assertFalse(any("/attachments/" in r[1] for r in server.requests))

    def test_uuid_rejected_before_http(self):
        with self.server() as server:
            self.assertIn("rejected=1", self.probe(server, "invalid"))
            self.assertFalse(any("/attachments/" in r[1] for r in server.requests))

    def test_video_and_embed_endpoints(self):
        with self.server(mime="video/mp4") as server:
            self.assertIn("ready=1", self.probe(server, "video"))
            self.assertTrue(any(r[1].endswith("/embeds/" + ID + "/video") for r in server.requests))
        with self.server(mime="application/json", body=b'{"image_data":null}') as server:
            self.assertIn("ready=1", self.probe(server, "embed"))

    def test_attachment_video_accepts_the_types_the_server_reports(self):
        # The server detects the type from the file's magic bytes, so a valid
        # video can arrive as a generic binary type.
        for mime in (
            "video/mp4",
            "video/webm",
            "video/ogg",
            "video/avi",
            "application/octet-stream",
            "application/ogg",
        ):
            with self.subTest(mime=mime), self.server(mime=mime) as server:
                self.assertIn("ready=1", self.probe(server, "attachment-video"))
        for mime in ("text/html", "image/png", "application/pdf"):
            with self.subTest(mime=mime), self.server(mime=mime) as server:
                self.assertIn("failed=1", self.probe(server, "attachment-video"))

    def test_embed_video_keeps_the_documented_allowlist(self):
        for mime in ("video/mp4", "video/webm", "video/ogg"):
            with self.subTest(mime=mime), self.server(mime=mime) as server:
                self.assertIn("ready=1", self.probe(server, "video"))
        for mime in ("application/octet-stream", "video/avi", "text/html"):
            with self.subTest(mime=mime), self.server(mime=mime) as server:
                self.assertIn("failed=1", self.probe(server, "video"))

    def test_public_webhook_avatar_has_no_cookie_and_uses_global_budget(self):
        with self.server() as server:
            output = self.probe(server, "avatar")
            self.assertIn("ready=1", output)
            self.assertIn("private=1", output)
            calls = [r for r in server.requests if "/webhooks/" in r[1]]
            self.assertEqual(len(calls), 1)
            self.assertFalse(calls[0][2])
        with self.server() as server:
            self.assertIn("cancelled=1", self.probe(server, "avatar-budget"))
            self.assertFalse(any("/webhooks/" in r[1] for r in server.requests))

    def test_public_avatar_rejects_unsafe_urls_before_http(self):
        for reference in (
            "//evil.test/avatar",
            "/auth/logout",
            "file:///etc/passwd",
            "http://evil.test/avatar",
            "https://user:pass@evil.test/avatar",
            "https://evil.test/avatar#fragment",
            "https://evil.test/\x1bavatar",
            "/webhooks/invalid/avatar",
        ):
            with self.subTest(reference=reference), self.server() as server:
                self.assertIn(
                    "rejected=1",
                    self.probe(server, "avatar-invalid", {"PAPO_AVATAR_REFERENCE": reference}),
                )
                self.assertFalse(any("/webhooks/" in r[1] for r in server.requests))

    def test_public_avatar_size_mime_and_redirect_limits(self):
        for status, mime, body in (
            (200, "text/html", b"html"),
            (200, "image/png", b"x" * (16 * 1024 * 1024 + 1)),
            (302, "image/png", b""),
        ):
            with (
                self.subTest(status=status, mime=mime),
                self.server(status=status, mime=mime, body=body) as server,
            ):
                self.assertIn("failed=1", self.probe(server, "avatar"))
                self.assertEqual(sum("/webhooks/" in r[1] for r in server.requests), 1)

    def test_external_https_avatar_never_receives_session_cookies_and_verifies_tls(self):
        with tempfile.TemporaryDirectory(prefix="papo-avatar-tls-") as directory:
            cert, key = Path(directory) / "cert.pem", Path(directory) / "key.pem"
            subprocess.run(
                [
                    "openssl",
                    "req",
                    "-x509",
                    "-newkey",
                    "rsa:2048",
                    "-nodes",
                    "-days",
                    "1",
                    "-keyout",
                    str(key),
                    "-out",
                    str(cert),
                    "-subj",
                    "/CN=127.0.0.1",
                    "-addext",
                    "subjectAltName=IP:127.0.0.1",
                ],
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(cert, key)
            with MockServer(tls_context=context) as external, self.server() as server:
                external.api_handler = lambda method, path, body: (
                    200,
                    b"fixture",
                    None,
                    None,
                    "image/png",
                )
                reference = external.url + "/webhooks/" + ID + "/avatar"
                env = {"PAPO_AVATAR_REFERENCE": reference, "CURL_CA_BUNDLE": str(cert)}
                self.assertIn("ready=1", self.probe(server, "avatar", env))
                self.assertEqual(len(external.requests), 1)
                self.assertFalse(external.requests[0][2])
                self.assertIn(
                    "failed=1", self.probe(server, "avatar", dict(env, CURL_CA_BUNDLE=""))
                )
                self.assertIn(
                    "failed=1",
                    self.probe(
                        server,
                        "avatar",
                        dict(
                            env, PAPO_AVATAR_REFERENCE=reference.replace("127.0.0.1", "localhost")
                        ),
                    ),
                )
                self.assertEqual(len(external.requests), 1)


if __name__ == "__main__":
    test_main(
        1,
    )
