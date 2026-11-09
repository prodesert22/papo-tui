from test_context import main as test_main

from test_context import argument
import subprocess
import os
import unittest

from mock_server import MockServer

PROBE = argument(0)


class AuthTests(unittest.TestCase):
    def run_probe(self, url, mode="", env=None):
        return subprocess.run(
            [PROBE, url, mode],
            capture_output=True,
            text=True,
            encoding="utf-8",
            timeout=20,
            env=env,
        )

    def test_oversized_inputs_are_rejected_before_network(self):
        for name, length in (
            ("PAPO_PROBE_USERNAME", 1025),
            ("PAPO_PROBE_PASSWORD", 4097),
            ("PAPO_PROBE_SERVER_PASSWORD", 4097),
        ):
            with self.subTest(field=name), MockServer() as server:
                result = self.run_probe(server.url, env=dict(os.environ, **{name: "a" * length}))
                self.assertIn("excedeu o limite do cliente", result.stdout)
                self.assertEqual(server.requests, [])

    def test_public_login_and_prefix(self):
        with MockServer() as server:
            result = self.run_probe(server.url + "///")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("ready=1", result.stdout)
            self.assertEqual(
                [r[1] for r in server.requests],
                ["/papo/server", "/papo/auth/login", "/papo/auth/whoami"],
            )
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

    def test_forbidden_identifies_login_stage_and_distinguishes_ip_block(self):
        for endpoint, scenario, mode, expected_requests, banned_notice, denied_notice in (
            (
                "/auth/login_server",
                "private",
                "private",
                2,
                "bloqueou este IP",
                "autorização da instância",
            ),
            ("/auth/login", "public", "", 2, "bloqueou a conta ou este IP", "acesso à conta"),
            ("/auth/whoami", "public", "", 3, "bloqueou esta conta", "validação da sessão"),
            ("/server", "public", "", 1, None, "acesso à instância"),
        ):
            for banned in (False, True) if banned_notice else (False,):
                with self.subTest(endpoint=endpoint, banned=banned), MockServer(scenario) as server:

                    def reject(_method, path, _body):
                        if path.endswith(endpoint):
                            return 403, {
                                "type": "https://papo.invalid/errors/"
                                + ("banned" if banned else "forbidden"),
                                "detail": "Synthetic!123\u001b[2J must never appear",
                            }
                        return None

                    server.api_handler = reject
                    result = self.run_probe(server.url, mode)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertIn("ready=0", result.stdout)
                    self.assertIn(banned_notice if banned else denied_notice, result.stdout)
                    self.assertNotIn("Synthetic!123", result.stdout)
                    self.assertNotIn("\u001b", result.stdout)
                    self.assertEqual(len(server.requests), expected_requests)

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
                    self.assertLessEqual(
                        index - previous + 1, 25 + 10 * (now - times[previous]) + 1
                    )

    def test_429_cooldown_survives_client_recreation_and_does_not_retry_mutations(self):
        for scenario, minimum in (("limited", 1), ("limited-malformed", 3), ("limited-date", 1)):
            with self.subTest(scenario=scenario), MockServer(scenario) as server:
                result = self.run_probe(server.url, "retry")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("Limite de requisições", result.stdout)
                self.assertIn("retry_ready=1", result.stdout)
                self.assertEqual(
                    [r[1] for r in server.requests],
                    ["/papo/server", "/papo/server", "/papo/auth/login", "/papo/auth/whoami"],
                )
                self.assertGreaterEqual(server.request_times[1] - server.request_times[0], minimum)

    TOKEN = "Zm9vYmFyLXN5bnRoZXRpYy10b2tlbi1fLTAxMjM0NTY3"
    ME = "00000000-0000-4000-8000-000000000001"

    def test_reset_link_token_is_read_without_network(self):
        cases = (
            (f"https://papo.example/passwordchange/{self.TOKEN}", self.TOKEN, "papo.example"),
            (f"  https://papo.example/passwordchange/{self.TOKEN}/ ", self.TOKEN, "papo.example"),
            (f"papo.example/passwordchange/{self.TOKEN}", self.TOKEN, "papo.example"),
            (self.TOKEN, self.TOKEN, ""),
        )
        for value, token, host in cases:
            with self.subTest(value=value):
                result = self.run_probe(value, "token")
                self.assertEqual(result.stdout.strip(), f"token={token} host={host}")
        for value in (
            "",
            "https://papo.example/outra/" + self.TOKEN,
            "https://papo.example/passwordchange/",
            f"https://papo.example/passwordchange/{self.TOKEN}?x=1",
            f"https://u:p@papo.example/passwordchange/{self.TOKEN}",
            f"ftp://papo.example/passwordchange/{self.TOKEN}",
            "token com espaço",
            "a" * 257,
        ):
            with self.subTest(value=value):
                self.assertEqual(self.run_probe(value, "token").stdout.strip(), "invalid")

    def reset_env(self, link):
        return dict(os.environ, PAPO_PROBE_RESET_LINK=link)

    def test_reset_link_sets_the_password_once_without_signing_in(self):
        with MockServer() as server:
            server.api_handler = lambda method, path, body: (
                (200, {"response": "Password updated successfully"})
                if path.endswith("/auth/password_reset")
                else None
            )
            link = f"https://papo.example/passwordchange/{self.TOKEN}"
            result = self.run_probe(server.url, "reset", self.reset_env(link))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("started=1 updated=1 session=0 input=0", result.stdout)
            self.assertIn("Senha redefinida", result.stdout)
            self.assertEqual(
                server.requests,
                [
                    (
                        "POST",
                        "/papo/auth/password_reset",
                        None,
                        {"token": self.TOKEN, "password": "Renewed!456"},
                    )
                ],
            )
            self.assertNotIn("Renewed!456", result.stdout)
            self.assertNotIn(self.TOKEN, result.stdout)

    def test_reset_link_failures_are_reported_and_never_repeated(self):
        for outcome, expected, field in (
            ((410, {"detail": "link expirado"}), "Link inválido, expirado ou já usado", 5),
            (
                (400, {"detail": "campo 'password'\u001b[2J deve ter 12 caracteres"}),
                "O servidor recusou a nova senha: campo 'password'",
                3,
            ),
            ((503, None), "não confirmou a redefinição", 0),
            ("drop", "Redefinição não confirmada", 0),
            ((429, None), "Limite de requisições", 0),
        ):
            with self.subTest(outcome=outcome), MockServer() as server:
                server.api_handler = lambda method, path, body, outcome=outcome: (
                    outcome if path.endswith("/auth/password_reset") else None
                )
                result = self.run_probe(server.url, "reset", self.reset_env(self.TOKEN))
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(f"started=1 updated=0 session=0 input={field}", result.stdout)
                self.assertIn(expected, result.stdout)
                self.assertNotIn("\u001b", result.stdout)
                self.assertNotIn("Renewed!456", result.stdout)
                self.assertEqual(len(server.requests), 1)

    def test_reset_link_input_is_checked_before_any_http(self):
        for link, password, field in (
            ("https://papo.example/sem-token", "Renewed!456", 5),
            (self.TOKEN, "fraca", 3),
            (self.TOKEN, "semmaiuscula!1", 3),
        ):
            with self.subTest(link=link, password=password), MockServer() as server:
                env = dict(self.reset_env(link), PAPO_PROBE_NEW_PASSWORD=password)
                result = self.run_probe(server.url, "reset", env)
                self.assertIn(f"started=0 updated=0 session=0 input={field}", result.stdout)
                self.assertEqual(server.requests, [])

    def password_backend(self, server, password_outcome=None, prepare_outcome=None):
        state = {"flag": False}

        def handle(method, path, body):
            if method == "POST" and path.endswith(f"/users/{self.ME}/reset"):
                if prepare_outcome:
                    return prepare_outcome
                state["flag"] = True
                return 200, {"response": "User password is set to reset"}
            if method == "PUT" and path.endswith(f"/users/{self.ME}/password"):
                if password_outcome:
                    return password_outcome
                if not state["flag"]:
                    return 409, {"type": "https://papo.invalid/errors/no-reset-password"}
                # The server revokes every session of the account, this one included.
                server.session_cookie = "revoked-by-password-change"
                return 200, {"response": "User password updated successfully"}
            return None

        server.api_handler = handle

    def test_own_password_change_prepares_then_ends_every_session(self):
        with MockServer() as server:
            self.password_backend(server)
            result = self.run_probe(server.url, "change")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("ready=1", result.stdout)
            self.assertIn("started=1 updated=1 session=0 rejected=1 outcome=0", result.stdout)
            self.assertIn("Senha alterada", result.stdout)
            changes = [r for r in server.requests if "/users/" in r[1]]
            self.assertEqual(
                [(r[0], r[1], r[3]) for r in changes],
                [
                    ("POST", f"/papo/users/{self.ME}/reset", None),
                    ("PUT", f"/papo/users/{self.ME}/password", {"password": "Renewed!456"}),
                ],
            )
            # Both use the signed-in session; nothing follows the change.
            self.assertTrue(all(r[2] == "Auth=user-fixture" for r in changes))
            self.assertEqual(server.requests[-1][1], f"/papo/users/{self.ME}/password")
            self.assertNotIn("Renewed!456", result.stdout)

    def test_own_password_change_failures_keep_the_session_and_never_repeat(self):
        for prepare, outcome, expected, state, puts in (
            (
                None,
                (400, {"detail": "senha\u001b]52;c;x\u0007 curta"}),
                "recusou a nova senha",
                2,
                1,
            ),
            (None, (409, None), "não preparou a troca de senha", 2, 1),
            (None, "drop", "Troca de senha não confirmada", 3, 1),
            (None, (500, None), "Troca de senha não confirmada", 3, 1),
            ((500, None), None, "não preparou a troca de senha", 2, 0),
            ((429, None), None, "Muitas tentativas", 2, 0),
        ):
            with self.subTest(prepare=prepare, outcome=outcome), MockServer() as server:
                self.password_backend(server, outcome, prepare)
                result = self.run_probe(server.url, "change")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(
                    f"started=1 updated=0 session=1 rejected=0 outcome={state}", result.stdout
                )
                self.assertIn(expected, result.stdout)
                self.assertNotIn("\u001b", result.stdout)
                self.assertNotIn("\u0007", result.stdout)
                self.assertEqual(
                    sum(r[1].endswith(f"/users/{self.ME}/reset") for r in server.requests), 1
                )
                self.assertEqual(
                    sum(r[1].endswith(f"/users/{self.ME}/password") for r in server.requests),
                    puts,
                )

    def test_own_password_change_checks_the_policy_before_any_http(self):
        with MockServer() as server:
            self.password_backend(server)
            env = dict(os.environ, PAPO_PROBE_NEW_PASSWORD="curta")
            result = self.run_probe(server.url, "change", env)
            self.assertIn("started=0 updated=0 session=1 rejected=0 outcome=2", result.stdout)
            self.assertIn("8 caracteres", result.stdout)
            self.assertFalse(any("/users/" in r[1] for r in server.requests))

    def test_url_validation(self):
        invalid = [
            "",
            "chat.example",
            "ftp://example.com",
            "http://example.com",
            "https://u:p@example.com",
            "https://example.com?q=x",
            "https://example.com/#x",
        ]
        for url in invalid:
            with self.subTest(url=url):
                self.assertEqual(self.run_probe(url, "normalize").returncode, 2)
        result = self.run_probe("https://example.com/papo///", "normalize")
        self.assertEqual(result.stdout.strip(), "https://example.com/papo")


if __name__ == "__main__":
    test_main(
        1,
    )
