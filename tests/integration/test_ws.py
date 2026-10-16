import os
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile
import unittest

from mock_server import MockServer

PROBE = sys.argv.pop(1)


class WebSocketTests(unittest.TestCase):
    def probe(self, server, mode="", env=None, url=None):
        result = subprocess.run(
            [PROBE, url or server.url, mode], capture_output=True, text=True, timeout=45, env=env
        )
        self.assertNotIn("Synthetic!123", result.stdout + result.stderr)
        self.assertNotIn("user-fixture", result.stdout + result.stderr)
        self.assertNotIn("temporary-fixture", result.stdout + result.stderr)
        return result

    def test_session_cookie_prefix_heartbeat_and_close(self):
        with MockServer(secure_cookie=True) as server:
            result = self.probe(server)
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            for value in (
                "rest_ready=1",
                "ws_open=1",
                "ws_status=101",
                "presence=1",
                "ack=1",
                "logout=1",
            ):
                self.assertIn(value, result.stdout)
            handshake = next(r for r in server.requests if r[1].endswith("/ws"))
            self.assertEqual(handshake[1:3], ("/papo/ws", "Auth=user-fixture"))
            self.assertIn(("close", b"\x03\xe8"), server.ws_controls)

    def test_fragments_chunks_utf8_and_interleaved_ping(self):
        with MockServer("ws-fragments") as server:
            result = self.probe(server, "fragments")
            self.assertIn("fragment=1", result.stdout)
            self.assertIn("ack=1", result.stdout)
            self.assertIn(("pong", b"probe"), server.ws_controls)

    def test_transport_errors_are_bounded_and_do_not_leak_secrets(self):
        scenarios = {
            "ws-unauthorized": "não foi autorizada",
            "ws-limited": "Limite de requisições",
            "ws-redirect": "redirecionou",
            "ws-invalid": "inválido",
            "ws-invalid-utf8": "inválido",
            "ws-http-overflow": "Não foi possível conectar",
            "ws-header-overflow": "excedeu o limite",
            "ws-binary": "incompatível",
            "ws-overflow": "excedeu o limite",
            "ws-disconnect": "conexão de tempo real",
        }
        for scenario, message in scenarios.items():
            with self.subTest(scenario=scenario), MockServer(scenario) as server:
                result = self.probe(server, "error")
                self.assertIn("ws_error=1", result.stdout)
                self.assertIn(message, result.stdout)
                self.assertEqual(sum(r[1].endswith("/ws") for r in server.requests), 1)

    def test_receive_and_send_queue_limits(self):
        for scenario, mode, message in (
            ("ws-queue", "queue", "fila de eventos"),
            ("public", "send-queue", "fila de envio"),
        ):
            with self.subTest(mode=mode), MockServer(scenario) as server:
                result = self.probe(server, mode)
                self.assertIn("ws_error=1", result.stdout)
                self.assertIn(message, result.stdout)

    def test_peer_close_is_acknowledged(self):
        with MockServer("ws-close") as server:
            result = self.probe(server, "close")
            self.assertIn("ws_closed=1", result.stdout)
            self.assertIn(("close", b"\x03\xe8"), server.ws_controls)

    def test_cancel_slow_and_queued_handshakes(self):
        for mode in ("cancel", "queued-cancel"):
            with self.subTest(mode=mode), MockServer("ws-slow") as server:
                result = self.probe(server, mode)
                self.assertIn("cancelled=1", result.stdout)
                if mode == "queued-cancel":
                    self.assertIn("queued=1", result.stdout)
                    self.assertFalse(any(r[1].endswith("/ws") for r in server.requests))

    def test_ack_and_incomplete_message_deadlines(self):
        for scenario, message in (
            ("ws-no-ack", "não confirmou"),
            ("ws-stall-fragment", "não chegou completo"),
        ):
            with self.subTest(scenario=scenario), MockServer(scenario) as server:
                result = self.probe(server, "error")
                self.assertIn("ws_error=1", result.stdout)
                self.assertIn(message, result.stdout)

    def test_heartbeat_ack_does_not_authenticate_revoked_session(self):
        with MockServer("ws-revoked") as server:
            result = self.probe(server, "revalidate")
            self.assertIn("ack=1", result.stdout)
            self.assertIn("revalidated_failure=1", result.stdout)
            self.assertEqual(sum(r[1].endswith("/auth/whoami") for r in server.requests), 1)
            self.assertEqual(sum(r[1].endswith("/auth/refresh") for r in server.requests), 1)

    def test_wss_trusted_ca_untrusted_certificate_and_hostname(self):
        with tempfile.TemporaryDirectory() as directory:
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
            trusted = dict(os.environ, CURL_CA_BUNDLE=str(cert))
            with MockServer(tls_context=context, secure_cookie=True) as server:
                result = self.probe(server, env=trusted)
                self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
                self.assertIn("ws_open=1", result.stdout)
                self.assertIn("ack=1", result.stdout)
                untrusted = dict(os.environ)
                untrusted.pop("CURL_CA_BUNDLE", None)
                result = self.probe(server, "transport", env=untrusted)
                self.assertIn("ws_error=1", result.stdout)
                self.assertIn("conexão segura", result.stdout)
                result = self.probe(
                    server,
                    "transport",
                    env=trusted,
                    url=server.url.replace("127.0.0.1", "localhost"),
                )
                self.assertIn("ws_error=1", result.stdout)
                self.assertIn("conexão segura", result.stdout)


if __name__ == "__main__":
    unittest.main()
