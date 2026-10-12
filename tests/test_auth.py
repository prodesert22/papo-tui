import subprocess
import os
import sys
import unittest

from mock_server import MockServer

PROBE = sys.argv.pop(1)


class AuthTests(unittest.TestCase):
    def run_probe(self, url, mode="", env=None):
        return subprocess.run([PROBE, url, mode], capture_output=True, text=True, timeout=20, env=env)

    def test_oversized_inputs_are_rejected_before_network(self):
        for name, length in (("PAPO_PROBE_USERNAME", 1025), ("PAPO_PROBE_PASSWORD", 4097),
                             ("PAPO_PROBE_SERVER_PASSWORD", 4097)):
            with self.subTest(field=name), MockServer() as server:
                result = self.run_probe(server.url, env=dict(os.environ, **{name: 'a' * length}))
                self.assertIn("excedeu o limite do cliente", result.stdout)
                self.assertEqual(server.requests, [])

    def test_public_login_and_prefix(self):
        with MockServer() as server:
            result = self.run_probe(server.url + "///")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("ready=1", result.stdout)
            self.assertEqual([r[1] for r in server.requests], ["/papo/server", "/papo/auth/login", "/papo/auth/whoami"])
            self.assertIsNone(server.requests[0][2])
            self.assertEqual(server.requests[-1][2], "Auth=user-fixture")

    def test_private_cookie_rotation(self):
        with MockServer("private") as server:
            result = self.run_probe(server.url, "private")
            self.assertIn("ready=1", result.stdout)
            self.assertEqual(server.requests[2][2], "Auth=temporary-fixture")
            self.assertEqual(server.requests[3][2], "Auth=user-fixture")

    def test_missing_server_password(self):
        with MockServer("private") as server:
            result = self.run_probe(server.url)
            self.assertIn("instância é privada", result.stdout)
            self.assertEqual(len(server.requests), 1)

    def test_logout_and_fresh_cookie_jar(self):
        with MockServer() as server:
            result = self.run_probe(server.url, "logout")
            self.assertIn("logout=1", result.stdout)
            self.assertIn("again=1", result.stdout)
            self.assertEqual(server.requests[3][1], "/papo/auth/logout")
            self.assertIsNone(server.requests[4][2])

    def test_invalid_response_rejected_credentials_and_session(self):
        for scenario in ("invalid", "rejected", "violation", "no-cookie", "oversized", "redirect"):
            with self.subTest(scenario=scenario), MockServer(scenario) as server:
                result = self.run_probe(server.url)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("ready=0", result.stdout)
                self.assertNotIn("Synthetic!123", result.stdout)

    def test_cancel_pending_transfer(self):
        with MockServer("slow") as server:
            result = self.run_probe(server.url, "cancel")
            self.assertIn("cancelled=1", result.stdout)

    def test_cancel_queued_request_does_not_reach_server(self):
        with MockServer() as server:
            result = self.run_probe(server.url, "queued-cancel")
            self.assertIn("cancelled=1", result.stdout)
            self.assertEqual(server.requests, [])

    def test_global_rate_survives_login_logout_and_client_recreation(self):
        with MockServer() as server:
            result = self.run_probe(server.url, "rate")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("rate_ready=1", result.stdout)
            self.assertEqual(len(server.requests), 51)
            times = server.request_times
            self.assertGreaterEqual(times[-1] - times[0], 2.5)
            for index, now in enumerate(times):
                for previous in range(index + 1):
                    self.assertLessEqual(index - previous + 1, 25 + 10 * (now - times[previous]) + 1)

    def test_429_cooldown_survives_client_recreation_and_does_not_retry_mutations(self):
        for scenario, minimum in (("limited", 1), ("limited-malformed", 3), ("limited-date", 1)):
            with self.subTest(scenario=scenario), MockServer(scenario) as server:
                result = self.run_probe(server.url, "retry")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("Limite de requisições", result.stdout)
                self.assertIn("retry_ready=1", result.stdout)
                self.assertEqual([r[1] for r in server.requests], ["/papo/server", "/papo/server", "/papo/auth/login", "/papo/auth/whoami"])
                self.assertGreaterEqual(server.request_times[1] - server.request_times[0], minimum)

    def test_url_validation(self):
        invalid = ["", "chat.example", "ftp://example.com", "http://example.com", "https://u:p@example.com", "https://example.com?q=x", "https://example.com/#x"]
        for url in invalid:
            with self.subTest(url=url):
                self.assertEqual(self.run_probe(url, "normalize").returncode, 2)
        result = self.run_probe("https://example.com/papo///", "normalize")
        self.assertEqual(result.stdout.strip(), "https://example.com/papo")


if __name__ == "__main__":
    unittest.main()
