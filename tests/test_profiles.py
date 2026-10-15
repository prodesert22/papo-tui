"""R3 persistence and discovery checks; all accounts and cookies are synthetic."""
import json
import os
from pathlib import Path
import socket
import ssl
import subprocess
import sys
import tempfile
import unittest

from mock_server import MockServer

PROBE = sys.argv.pop(1)


class ProfilesTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="papo-profiles-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.path = self.root / "papo-tui" / "config.json"
        self.environment = dict(os.environ, XDG_CONFIG_HOME=str(self.root), NO_PROXY="*")
        for key in ("ALL_PROXY", "http_proxy", "https_proxy", "HTTP_PROXY", "HTTPS_PROXY"):
            self.environment.pop(key, None)

    def probe(self, *arguments, environment=None, timeout=25):
        result = subprocess.run([PROBE, *arguments], env=environment or self.environment,
                                capture_output=True, text=True, timeout=timeout)
        self.assertEqual(result.returncode, 0, result.stderr)
        for secret in ("Synthetic!123", "user-fixture", "temporary-fixture"):
            self.assertNotIn(secret, result.stdout + result.stderr)
        return json.loads(result.stdout)

    def seed(self, value):
        self.path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        self.path.write_text(json.dumps(value) if not isinstance(value, str) else value)
        self.path.chmod(0o600)

    @staticmethod
    def configuration(profiles=(), version=2, selected=None):
        result = {"version": version, "profiles": list(profiles), "selected_profile": selected}
        if version == 2: result.update(requests_per_second=10, request_burst=25)
        return result

    def add(self, name="Comunidade", url="https://example.com:8443/papo///", allow="deny"):
        result = self.probe("add", name, url, allow)
        self.assertTrue(result["ok"], result["message"])
        return result["id"]

    def test_crud_ids_restart_selection_and_no_credentials(self):
        first = self.add()
        second = self.add("Trabalho", "https://example.com:9443/papo")
        self.assertNotEqual(first, second)
        self.assertEqual(len(first), 32)
        result = self.probe("edit", first, "Renomeado 界", "https://example.com:8443/new-prefix", "deny")
        self.assertTrue(result["ok"])
        self.assertEqual(result["id"], first)
        self.assertTrue(self.probe("select", first, "deny")["ok"])
        restored = self.probe("list")
        self.assertEqual(restored["selected"], first)
        self.assertEqual(restored["profiles"][0], {"id": first, "name": "Renomeado 界", "url": "https://example.com:8443/new-prefix"})
        self.assertEqual(self.path.stat().st_mode & 0o7777, 0o600)
        self.assertEqual(self.path.parent.stat().st_mode & 0o7777, 0o700)
        persisted = json.loads(self.path.read_text())
        self.assertEqual(set(persisted), {"version", "profiles", "selected_profile", "requests_per_second", "request_burst", "desktop_notifications", "notification_preview"})
        self.assertTrue(all(set(item) == {"id", "name", "url"} for item in persisted["profiles"]))
        self.assertEqual(list(self.path.parent.glob("config.tmp.*")), [])
        self.assertTrue(self.probe("remove", first)["ok"])
        restored = self.probe("list")
        self.assertEqual(restored["selected"], "")
        self.assertEqual([item["id"] for item in restored["profiles"]], [second])

    def test_url_and_name_validation_leave_previous_file_unchanged(self):
        self.add()
        previous = self.path.read_bytes()
        urls = ("", "example.com", "ftp://example.com", "http://example.com", "https://u:p@example.com",
                "https://@example.com", "https://example.com?", "https://example.com/#", "https://example.com?q=x",
                "https://example.com:65536", "https://example.com:wrong", "https://example.com:-1")
        for url in urls:
            with self.subTest(url=url):
                self.assertFalse(self.probe("add", "Invalid", url, "allow")["ok"])
                self.assertEqual(self.path.read_bytes(), previous)
        for name in ("", "   ", "a\nname", "a\x1bname", "a\x7fname", "a\u0085name", "é" * 65):
            with self.subTest(name=repr(name)):
                self.assertFalse(self.probe("add", name, "https://example.com", "deny")["ok"])
                self.assertEqual(self.path.read_bytes(), previous)
        self.add("é" * 64, "https://example.com")

    def test_local_http_requires_explicit_policy_for_add_edit_and_select(self):
        url = "http://127.0.0.1:8080/prefix"
        self.assertFalse(self.probe("add", "Local", url, "deny")["ok"])
        first = self.add("Local", url, "allow")
        self.assertEqual(self.probe("list")["profiles"][0]["url"], url)
        self.assertFalse(self.probe("select", first, "deny")["ok"])
        self.assertTrue(self.probe("select", first, "allow")["ok"])
        self.assertFalse(self.probe("edit", first, "Local", url, "deny")["ok"])
        self.add("IPv6", "http://[::1]:8080/papo", "allow")

    def test_rate_preferences_validation_restart_and_v1_migration(self):
        first = self.add()
        legacy = self.configuration(json.loads(self.path.read_text())["profiles"], version=1, selected=first)
        self.seed(legacy)
        before = self.path.read_bytes()
        self.assertEqual(self.probe("list")["requests_per_second"], 10)
        self.assertEqual(self.path.read_bytes(), before)
        self.assertTrue(self.probe("rate", "2.5", "3")["ok"])
        self.assertEqual(json.loads(self.path.read_text())["version"], 3)
        restored = self.probe("list")
        self.assertEqual((restored["requests_per_second"], restored["request_burst"], restored["selected"]), (2.5, 3, first))
        before = self.path.read_bytes()
        for rate, burst in (("0", "1"), ("-1", "1"), ("NaN", "1"), ("1e999", "1"), ("true", "1"),
                            ("2", "0"), ("2", "-1"), ("2", "1.5"), ("2", "4294967296"), ("2", "true")):
            with self.subTest(rate=rate, burst=burst):
                self.assertFalse(self.probe("rate", rate, burst)["ok"])
                self.assertEqual(self.path.read_bytes(), before)
        self.assertTrue(self.probe("rate", "10", "25")["ok"])

    def test_notifications_defaults_v2_migration_restart_and_invalid_boolean(self):
        self.seed(self.configuration())
        before=self.path.read_bytes()
        defaults=self.probe('list')
        self.assertTrue(defaults['desktop_notifications']); self.assertTrue(defaults['notification_preview'])
        self.assertEqual(before,self.path.read_bytes())
        self.assertTrue(self.probe('notifications','false','false')['ok'])
        loaded=self.probe('list'); self.assertFalse(loaded['desktop_notifications']); self.assertFalse(loaded['notification_preview'])
        valid=json.loads(self.path.read_text()); self.assertEqual(valid['version'],3)
        valid['notification_preview']='false'; self.seed(valid); before=self.path.read_bytes()
        self.assertFalse(self.probe('list')['writable'])
        self.assertFalse(self.probe('notifications','true','true')['ok']); self.assertEqual(before,self.path.read_bytes())

    def test_invalid_configuration_is_preserved_and_partial_data_discarded(self):
        bad_profile = {"id": "0" * 32, "name": "Valid", "url": "https://example.com"}
        malformed = self.configuration([bad_profile, {"id": "wrong"}])
        malformed.update(requests_per_second=3.5, request_burst=2)
        cases = ("{broken", {"version": 999}, malformed, self.configuration([bad_profile, bad_profile]),
                 {**self.configuration(), "requests_per_second": 0},
                 {**self.configuration(), "request_burst": 1.5},
                 {**self.configuration(), "password": "forbidden-fixture"})
        for value in cases:
            with self.subTest(value=value):
                self.seed(value)
                before = self.path.read_bytes()
                loaded = self.probe("list")
                self.assertFalse(loaded["writable"])
                self.assertEqual(loaded["profiles"], [])
                self.assertEqual((loaded["requests_per_second"], loaded["request_burst"]), (10, 25))
                self.assertFalse(self.probe("add", "New", "https://example.com", "deny")["ok"])
                self.assertEqual(self.path.read_bytes(), before)
        self.seed(self.configuration())
        self.assertTrue(self.probe("list")["writable"])

    def test_links_fifo_and_oversized_configuration_are_rejected(self):
        self.path.parent.mkdir(mode=0o700)
        outside = self.root / "outside.json"
        outside.write_text(json.dumps(self.configuration()))
        original = outside.read_bytes()
        self.path.symlink_to(outside)
        self.assertFalse(self.probe("list")["writable"])
        self.path.unlink()
        os.link(outside, self.path)
        self.assertFalse(self.probe("list")["writable"])
        self.path.unlink()
        os.mkfifo(self.path)
        self.assertFalse(self.probe("list", timeout=3)["writable"])
        self.path.unlink()
        self.seed(" " * (256 * 1024 + 1))
        self.assertFalse(self.probe("list")["writable"])
        self.assertEqual(outside.read_bytes(), original)
        self.path.unlink()
        self.path.parent.rmdir()
        self.path.parent.symlink_to(self.root)
        self.assertFalse(self.probe("list")["writable"])

    def test_concurrent_writers_lock_and_profile_limit(self):
        result = self.probe("stale")
        self.assertFalse(result["ok"])
        self.assertIn("outro processo", result["message"])
        self.assertEqual([p["name"] for p in self.probe("list")["profiles"]], ["Other writer"])
        before = self.path.read_bytes()
        self.assertFalse(self.probe("locked")["ok"])
        self.assertEqual(self.path.read_bytes(), before)
        items = [{"id": f"{i:032x}", "name": f"Perfil {i}", "url": "https://example.com"} for i in range(64)]
        self.seed(self.configuration(items))
        before = self.path.read_bytes()
        self.assertFalse(self.probe("add", "Overflow", "https://example.com", "deny")["ok"])
        self.assertEqual(self.path.read_bytes(), before)

    def test_home_fallback_is_isolated_and_permissions_are_repaired(self):
        environment = dict(self.environment, HOME=str(self.root), XDG_CONFIG_HOME="relative")
        result = self.probe("add", "Fallback", "https://example.com", "deny", environment=environment)
        self.assertTrue(result["ok"])
        path = self.root / ".config" / "papo-tui" / "config.json"
        self.assertTrue(path.is_file())
        path.chmod(0o644)
        path.parent.chmod(0o755)
        self.assertTrue(self.probe("list", environment=environment)["writable"])
        self.assertEqual(path.stat().st_mode & 0o7777, 0o600)
        self.assertEqual(path.parent.stat().st_mode & 0o7777, 0o700)

    def test_public_private_discovery_prefix_and_no_login(self):
        for scenario, public in (("public", True), ("private", False)):
            with self.subTest(scenario=scenario), MockServer(scenario) as server:
                result = self.probe("discover", server.url + "///", "allow")
                self.assertTrue(result["identified"], result["message"])
                self.assertEqual(result["name"], "Comunidade")
                self.assertEqual(result["public"], public)
                self.assertEqual([r[1] for r in server.requests], ["/papo/health", "/papo/server"])
                self.assertTrue(all(r[0] == "GET" and r[2] is None for r in server.requests))

    def test_discovery_errors_and_explicit_http_policy(self):
        for scenario, message in (("no-health", "/health"), ("unavailable", "indisponível"),
                                  ("uninitialized", "não inicializada"), ("invalid", "incompatível"),
                                  ("missing-public", "incompatível"), ("oversized", "grande demais"),
                                  ("health-limited", "Limite de requisições")):
            with self.subTest(scenario=scenario), MockServer(scenario) as server:
                result = self.probe("discover", server.url, "allow")
                self.assertFalse(result["identified"])
                self.assertIn(message, result["message"])
                self.assertLessEqual(len(server.requests), 2)
        with MockServer() as server:
            self.assertIn("--allow-local-http", self.probe("discover", server.url)["message"])
            self.assertEqual(server.requests, [])
        result = self.probe("discover", "https://papo-r3-does-not-exist.invalid")
        self.assertIn("DNS", result["message"])
        with socket.socket() as unused:
            unused.bind(("127.0.0.1", 0))
            port = unused.getsockname()[1]
        self.assertIn("recusada", self.probe("discover", f"http://127.0.0.1:{port}", "allow")["message"])
        with MockServer("timeout") as server:
            self.assertIn("demorou", self.probe("discover", server.url, "allow")["message"])

    def test_redirect_does_not_contact_another_origin(self):
        with MockServer("redirect") as source, MockServer() as target:
            source.redirect_url = target.url
            self.assertIn("redirecionou", self.probe("discover", source.url, "allow")["message"])
            self.assertEqual(target.requests, [])

    def test_old_cookie_store_socket_and_delayed_responses_are_discarded(self):
        with MockServer() as source, MockServer() as target:
            target.name = "Outra instância"
            result = self.probe("switch", source.url, target.url)
            self.assertTrue(result["old_ready"])
            self.assertTrue(result["old_ws_open"])
            self.assertTrue(result["identified"])
            self.assertEqual(result["name"], target.name)
            self.assertTrue(all(r[2] is None for r in target.requests))
            self.assertTrue(any(r[1].endswith("/ws") for r in source.requests))
            self.assertFalse(source.ws_connected.is_set())
        with MockServer("slow-health") as source, MockServer() as target:
            source.name, target.name = "Resposta antiga", "Resposta atual"
            result = self.probe("pending", source.url, target.url)
            self.assertTrue(result["identified"])
            self.assertEqual(result["name"], "Resposta atual")
            self.assertEqual([r[1] for r in source.requests], ["/papo/health"])

    def test_global_rate_and_queued_cancellation_survive_client_recreation(self):
        with MockServer() as server:
            result = self.probe("queued-cancel", server.url)
            self.assertTrue(result["cancelled"])
            self.assertEqual(server.requests, [])
        with MockServer() as server:
            result = self.probe("rate-discovery", server.url)
            self.assertTrue(result["identified"])
            self.assertEqual(len(server.requests), 6)
            gaps = [b - a for a, b in zip(server.request_times, server.request_times[1:])]
            self.assertTrue(all(gap >= 0.45 for gap in gaps), gaps)

    def test_discovery_tls_ca_and_hostname_are_verified(self):
        cert, key = self.root / "cert.pem", self.root / "key.pem"
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                        "-keyout", str(key), "-out", str(cert), "-subj", "/CN=127.0.0.1",
                        "-addext", "subjectAltName=IP:127.0.0.1"], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, key)
        trusted = dict(self.environment, CURL_CA_BUNDLE=str(cert))
        untrusted = dict(self.environment)
        untrusted.pop("CURL_CA_BUNDLE", None)
        with MockServer(tls_context=context) as server:
            self.assertTrue(self.probe("discover", server.url, environment=trusted)["identified"])
            self.assertIn("segura", self.probe("discover", server.url, environment=untrusted)["message"])
            self.assertIn("segura", self.probe("discover", server.url.replace("127.0.0.1", "localhost"), environment=trusted)["message"])


if __name__ == "__main__":
    unittest.main()
