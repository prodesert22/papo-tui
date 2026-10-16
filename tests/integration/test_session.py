"""R4 integration with explicitly synthetic accounts and opaque cookies."""

import json
import os
import subprocess
import sys
import unittest
from mock_server import MockServer

BINARY = sys.argv.pop(1)


class Sessions(unittest.TestCase):
    def probe(self, server, mode, **settings):
        environment = dict(os.environ, LC_ALL="C.UTF-8", **settings)
        result = subprocess.run(
            [BINARY, server.url, mode], env=environment, capture_output=True, text=True, timeout=25
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("Synthetic!123", result.stdout + result.stderr)
        self.assertNotIn("user-fixture", result.stdout + result.stderr)
        return json.loads(result.stdout)

    def count(self, server, path):
        return sum(r[1].endswith(path) for r in server.requests)

    def test_registration_requires_explicit_login_and_clears_cookie(self):
        with MockServer() as server:
            result = self.probe(server, "register")
            self.assertTrue(result["registered"])
            self.assertFalse(result["session"])
            self.assertEqual(self.count(server, "/auth/login"), 0)
            self.assertEqual(self.count(server, "/auth/whoami"), 0)
        with MockServer() as server:
            result = self.probe(server, "register-login")
            self.assertTrue(result["registered"] and result["session"])
            login = next(r for r in server.requests if r[1].endswith("/auth/login"))
            self.assertIsNone(login[2])

    def test_local_policy_before_http(self):
        for settings, field in (
            ({"PAPO_PROBE_USERNAME": "ab"}, 2),
            ({"PAPO_PROBE_PASSWORD": "A!1"}, 3),
            ({"PAPO_PROBE_PASSWORD": "synthetic!123"}, 3),
            ({"PAPO_PROBE_PASSWORD": "Synthetic123"}, 3),
        ):
            with self.subTest(settings=settings), MockServer() as server:
                result = self.probe(server, "register", **settings)
                self.assertEqual(result["input_error"], field)
                self.assertEqual(server.requests, [])

    def test_remote_policy_conflict_and_rate_limit_not_replayed(self):
        for scenario, field in (
            ("register-conflict", 2),
            ("register-policy", 0),
            ("register-limited", 0),
        ):
            with self.subTest(scenario=scenario), MockServer(scenario) as server:
                result = self.probe(server, "register")
                self.assertFalse(result["registered"])
                self.assertEqual(result["input_error"], field)
                self.assertEqual(self.count(server, "/auth/register"), 1)

    def test_private_registration_and_temporary_auth_expiration(self):
        for scenario, expected in (("private", True), ("private-expired", False)):
            with self.subTest(scenario=scenario), MockServer(scenario) as server:
                result = self.probe(server, "register", PAPO_PROBE_SERVER_PASSWORD="Server!123")
                self.assertEqual(result["registered"], expected)
                self.assertEqual(self.count(server, "/auth/login_server"), 1)
                request = next(r for r in server.requests if r[1].endswith("/auth/register"))
                self.assertEqual(request[2], "Auth=temporary-fixture")
                if not expected:
                    self.assertEqual(result["input_error"], 4)
        with MockServer("private-expired") as server:
            result = self.probe(server, "login", PAPO_PROBE_SERVER_PASSWORD="Server!123")
            self.assertEqual(result["input_error"], 4)
            self.assertEqual(self.count(server, "/auth/login"), 1)

    def test_refresh_serialized_and_new_cookie_confirmed(self):
        with MockServer() as server:
            result = self.probe(server, "refresh")
            self.assertTrue(
                result["initial"] and result["session"] and result["concurrent_blocked"]
            )
            self.assertEqual(server.refresh_count, 1)
            self.assertEqual(result["generation"], result["final_generation"])
            whoami = [r for r in server.requests if r[1].endswith("/auth/whoami")]
            self.assertEqual(whoami[-1][2], "Auth=rotated-fixture-1")

    def test_refresh_failure_returns_login_without_loop(self):
        for scenario in (
            "refresh-revoked",
            "refresh-ban",
            "refresh-limited",
            "refresh-no-cookie",
            "refresh-expiry",
            "refresh-identity",
            "refresh-violation",
            "refresh-unconfirmed",
        ):
            with self.subTest(scenario=scenario), MockServer(scenario) as server:
                result = self.probe(server, "refresh")
                self.assertTrue(result["initial"])
                self.assertFalse(result["session"])
                self.assertEqual(result["identity"], "")
                self.assertGreater(result["final_generation"], result["generation"])
                self.assertEqual(server.refresh_count, 1)

    def test_get_recovered_once_and_response_owned_by_context(self):
        with MockServer("api-recover") as server:
            result = self.probe(server, "api")
            self.assertTrue(result["session"] and result["concurrent_blocked"])
            self.assertEqual(result["status"], 200)
            self.assertEqual(result["response_generation"], result["generation"])
            self.assertEqual(server.refresh_count, 1)
            self.assertEqual(server.api_count, 2)
            self.assertEqual(server.requests[-1][2], "Auth=rotated-fixture-1")
        with MockServer("api-twice") as server:
            result = self.probe(server, "api")
            self.assertFalse(result["session"])
            self.assertEqual(server.refresh_count, 1)
            self.assertEqual(server.api_count, 2)

    def test_api_permission_and_rate_do_not_logout_or_repeat(self):
        for scenario, status in (("api-permission", 403), ("api-limited", 429)):
            with self.subTest(scenario=scenario), MockServer(scenario) as server:
                result = self.probe(server, "api")
                self.assertTrue(result["session"])
                self.assertEqual(result["status"], status)
                self.assertEqual(server.refresh_count, 0)
                self.assertEqual(server.api_count, 1)

    def test_api_ban_and_violation_clear_identity(self):
        for scenario in ("api-ban", "api-violation"):
            with self.subTest(scenario=scenario), MockServer(scenario) as server:
                result = self.probe(server, "api")
                self.assertFalse(result["session"])
                self.assertEqual(result["identity"], "")
                self.assertEqual(server.refresh_count, 0)

    def test_logout_uses_rotated_cookie_and_handles_remote_failure(self):
        with MockServer("refresh-slow") as server:
            result = self.probe(server, "logout-refresh")
            self.assertTrue(result["logout_started"])
            self.assertFalse(result["session"])
            self.assertEqual(server.requests[-1][1], "/papo/auth/logout")
            self.assertEqual(server.requests[-1][2], "Auth=rotated-fixture-1")
            self.assertEqual(self.count(server, "/auth/whoami"), 1)
        with MockServer("logout-fail") as server:
            result = self.probe(server, "logout")
            self.assertTrue(result["logout_started"])
            self.assertFalse(result["session"])
            self.assertIn("não confirmada", result["message"])
            self.assertEqual(result["identity"], "")

    def test_short_expiry_automatically_renews_without_busy_loop(self):
        with MockServer("refresh-short") as server:
            result = self.probe(server, "automatic")
            self.assertTrue(result["session"])
            self.assertEqual(server.refresh_count, 2)

    def test_websocket_survives_rotation_and_uses_same_identity(self):
        with MockServer() as server:
            result = self.probe(server, "ws-refresh")
            self.assertTrue(result["ws_open"] and result["session"])
            self.assertEqual(server.refresh_count, 1)
            self.assertEqual(self.count(server, "/ws"), 1)

    def test_refresh_requests_share_the_global_budget(self):
        with MockServer() as server:
            result = self.probe(server, "rate")
            self.assertTrue(result["session"])
            self.assertEqual(server.refresh_count, 14)
            self.assertEqual(len(server.request_times), 31)
            self.assertGreaterEqual(server.request_times[-1] - server.request_times[0], 0.55)

    def test_context_switch_clears_cookie_and_changes_generation(self):
        with MockServer() as server:
            result = self.probe(server, "switch")
            self.assertTrue(result["session"])
            self.assertGreater(result["final_generation"], result["generation"])
            logins = [r for r in server.requests if r[1].endswith("/auth/login")]
            self.assertEqual([r[2] for r in logins], [None, None])


if __name__ == "__main__":
    unittest.main()
