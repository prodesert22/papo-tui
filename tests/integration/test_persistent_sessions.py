"""Synthetic HTTP and desktop credentials, exclusively on a private D-Bus."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

KEYRING = sys.argv.pop(2)
from test_terminal import Terminal  # consumes the TUI binary  # noqa: E402 - path and test arguments are prepared above
from history_mock import HistoryMock  # noqa: E402 - path and test arguments are prepared above


class Persistence(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="papo-private-session-test-")
        self.cache = tempfile.TemporaryDirectory(prefix="papo-font-cache-test-")
        root = Path(self.directory.name)
        self.env = dict(
            DBUS_SESSION_BUS_ADDRESS=os.environ["DBUS_SESSION_BUS_ADDRESS"],
            HOME=str(root),
            XDG_CACHE_HOME=self.cache.name,
            XDG_CONFIG_HOME=str(root / "config"),
            XDG_STATE_HOME=str(root / "state"),
        )
        self.provider = None
        self.provider_type = "wallet6"
        self.terminals = []

    def tearDown(self):
        for t in self.terminals:
            if t.process.poll() is None:
                t.close()
        if self.provider:
            self.provider.terminate()
            _, error = self.provider.communicate(timeout=5)
            if os.environ.get("PAPO_TEST_DBUS_TRACE"):
                sys.stderr.write(error.decode())
            self.assertNotIn(b"Synthetic!123", error)
            self.assertNotIn(b"user-fixture", error)
        self.directory.cleanup()
        self.cache.cleanup()

    def keyring(self, kind="wallet6"):
        self.provider_type = kind
        self.provider = subprocess.Popen(
            [KEYRING, kind],
            env=dict(os.environ, **self.env),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.assertEqual(self.provider.stdout.readline(), b"ready\n")

    def control(self, method, value=None):
        dest = (
            "org.freedesktop.secrets"
            if self.provider_type == "secret"
            else "org.kde.kwalletd" + self.provider_type[-1]
        )
        path = "/modules/kwalletd5" if self.provider_type == "wallet5" else "/modules/kwalletd6"
        command = [
            "gdbus",
            "call",
            "--session",
            "--dest",
            dest,
            "--object-path",
            path,
            "--method",
            "org.papo.SessionTest." + method,
        ]
        if value is not None:
            command.append(value)
        return subprocess.check_output(
            command, env=dict(os.environ, **self.env), text=True, timeout=5
        )

    def terminal(self, **settings):
        t = Terminal(environment=dict(self.env, **settings))
        self.terminals.append(t)
        return t

    def wait(self, t, text):
        deadline = time.monotonic() + 8
        while text not in "\n".join(t.screen.display) and time.monotonic() < deadline:
            t.read(0.03)
        self.assertIn(text, "\n".join(t.screen.display))

    def state(self):
        files = list(Path(self.env["XDG_STATE_HOME"]).glob("papo-tui/session-*.json"))
        return json.loads(files[0].read_text()) if files else {}

    def saved(self, t, url):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            s = self.state()
            if s.get("url") == url and s.get("restore_allowed"):
                return
            t.read(0.03)
        self.fail("Credential was not committed; notice=" + "\n".join(t.screen.display))

    def login(self, server):
        server.cookie_max_age = 30 * 86400
        if self.state():
            state = self.state()
            state["restore_allowed"] = False
            next(Path(self.env["XDG_STATE_HOME"]).glob("papo-tui/session-*.json")).write_text(
                json.dumps(state)
            )
        t = self.terminal()
        if server.url in "\n".join(t.screen.display):
            t.send("\x1b[Z\x15")
        t.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(t, "geral")
        self.saved(t, server.url)
        return t

    def login_count(self, server):
        return sum(r[1].endswith("/auth/login") for r in server.requests)

    def test_native_wallet6_restart_and_no_cookie_on_disk(self):
        self.keyring()
        with HistoryMock() as server:
            t = self.login(server)
            self.assertTrue(t.close())
            t = self.terminal()
            self.wait(t, "geral")
            self.assertEqual(self.login_count(server), 1)
            self.wait(t, "Mensagem 0250")
            self.assertTrue(any("/messages" in r[1] for r in server.requests))
            for file in Path(self.directory.name).rglob("*"):
                if file.is_file():
                    data = file.read_bytes()
                    self.assertNotIn(b"user-fixture", data)
                    self.assertNotIn(b"Synthetic!123", data)
                    self.assertEqual(file.stat().st_mode & 0o777, 0o600)
            self.assertNotIn(b"user-fixture", t.output)
            self.assertTrue(t.close())

    def test_native_wallet5_restart(self):
        self.keyring("wallet5")
        with HistoryMock() as server:
            t = self.login(server)
            self.assertTrue(t.close())
            t = self.terminal()
            self.wait(t, "geral")
            self.assertEqual(self.login_count(server), 1)
            self.assertTrue(t.close())

    def test_saved_session_restores_when_new_logins_are_ip_blocked(self):
        self.keyring()
        with HistoryMock() as server:
            t = self.login(server)
            self.assertTrue(t.close())
            route = server.api_handler

            def reject_new_logins(method, path, body):
                if method == "POST" and path.endswith(("/auth/login", "/auth/login_server")):
                    return 403, {"type": "https://papo.invalid/errors/banned"}
                return route(method, path, body)

            server.api_handler = reject_new_logins
            start = len(server.requests)
            t = self.terminal()
            self.wait(t, "geral")
            self.wait(t, "Mensagem 0250")
            self.assertEqual(self.login_count(server), 1)
            self.assertTrue(any(r[1].endswith("/auth/whoami") for r in server.requests[start:]))
            self.assertFalse(any(r[1].endswith("/auth/login_server") for r in server.requests))
            self.assertTrue(t.close())

    @unittest.skipUnless(os.environ.get("PAPO_TEST_SECRET"), "libsecret adapter not built")
    def test_secret_service_restart(self):
        self.keyring("secret")
        with HistoryMock() as server:
            t = self.login(server)
            self.assertTrue(t.close())
            t = self.terminal()
            self.wait(t, "geral")
            self.assertEqual(self.login_count(server), 1)
            self.assertTrue(t.close())

    def test_absent_provider_remembers_url_but_requires_login(self):
        with HistoryMock() as server:
            t = self.terminal()
            t.send(server.url + "\tjosé\tSynthetic!123\n")
            self.wait(t, "geral")
            t.read(0.2)
            self.assertTrue(t.close())
            t = self.terminal()
            self.wait(t, server.url)
            self.assertIn("URL do servidor", "\n".join(t.screen.display))
            self.assertNotIn("geral", "\n".join(t.screen.display))
            self.assertFalse(self.state().get("restore_allowed"))
            self.assertEqual(self.login_count(server), 1)
            self.assertTrue(t.close())

    def test_refreshes_an_expired_jwt_once_and_stores_rotated_cookie(self):
        self.keyring()
        with HistoryMock() as server:
            t = self.login(server)
            t.close()
            route = server.route
            expired = True

            def route_expired(method, path, body):
                nonlocal expired
                if method == "GET" and path.endswith("/auth/whoami") and expired:
                    expired = False
                    return 401, None
                return route(method, path, body)

            server.api_handler = route_expired
            t = self.terminal()
            self.wait(t, "geral")
            self.saved(t, server.url)
            self.assertEqual(server.refresh_count, 1)
            self.assertTrue(
                any(
                    r[2] == "Auth=rotated-fixture-1"
                    for r in server.requests
                    if r[1].endswith("/auth/whoami")
                )
            )
            t.close()
            t = self.terminal()
            self.wait(t, "geral")
            self.assertEqual(self.login_count(server), 1)
            self.assertEqual(server.refresh_count, 1)
            t.close()

    def test_logout_forgets_saved_session(self):
        self.keyring()
        with HistoryMock() as server:
            t = self.login(server)
            t.send("\x0c")
            self.wait(t, "Você saiu da conta")
            t.close()
            self.assertFalse(self.state()["restore_allowed"])
            t = self.terminal()
            self.wait(t, server.url)
            self.assertIn("URL do servidor", "\n".join(t.screen.display))
            self.assertEqual(self.login_count(server), 1)
            t.close()

    def test_foreign_snapshot_and_identity_are_rejected(self):
        self.keyring()
        with HistoryMock() as server:
            for kind in ["identity", "url"]:
                t = self.login(server)
                t.close()
                self.control("Tamper", kind)
                before = len(server.requests)
                t = self.terminal()
                self.wait(t, "Entre novamente")
                t.read(0.2)
                self.assertIn("URL do servidor", "\n".join(t.screen.display))
                self.assertNotIn("geral", "\n".join(t.screen.display))
                if kind == "url":
                    self.assertEqual(len(server.requests), before)
                t.close()

    def test_last_server_only_and_second_window_is_memory_only(self):
        self.keyring()
        with HistoryMock() as first, HistoryMock() as second:
            second.name = "Trabalho"
            second.cookie_max_age = 30 * 86400
            t = self.login(first)
            extra = self.terminal()
            self.wait(extra, "URL do servidor")
            self.assertNotIn("geral", "\n".join(extra.screen.display))
            self.assertEqual(self.login_count(first), 1)
            extra.close()
            row = t.screen.display[1]
            t.click(1, row.index("[+ Adicionar]") + 2)
            self.wait(t, "URL do servidor")
            t.send(second.url + "\tjosé\tSynthetic!123\n")
            self.wait(t, "[Trabalho]")
            self.saved(t, second.url)
            t.close()
            before = len(first.requests)
            t = self.terminal()
            self.wait(t, "[Trabalho]")
            self.assertNotIn("Comunidade", t.screen.display[1])
            self.assertEqual(len(first.requests), before)
            self.assertEqual(self.login_count(second), 1)
            t.close()

    def test_failed_write_and_locked_provider_fall_back_to_login(self):
        self.keyring()
        with HistoryMock() as server:
            t = self.login(server)
            t.close()
            self.control("Mode", "locked")
            t = self.terminal()
            self.wait(t, server.url)
            t.read(0.2)
            self.assertIn("URL do servidor", "\n".join(t.screen.display))
            t.close()
            self.control("Mode", "fail")
            # Public metadata can point to a valid previous session; prevent
            # automatic restoration while exercising a new login.
            state = self.state()
            state["restore_allowed"] = False
            file = next(Path(self.env["XDG_STATE_HOME"]).glob("papo-tui/session-*.json"))
            file.write_text(json.dumps(state))
            t = self.terminal()
            t.send("josé\tSynthetic!123\n")  # URL prefilled, focus username
            self.wait(t, "geral")
            t.read(0.3)
            t.close()
            self.assertFalse(self.state()["restore_allowed"])

    def test_crash_before_marker_commit_does_not_reuse_cookie(self):
        self.keyring()
        with HistoryMock() as server:
            t = self.login(server)
            self.control("Mode", "slow")
            t.send("\x1b[17~")
            self.wait(t, "[Renovar]")
            t.send("\x12")
            deadline = time.monotonic() + 5
            while server.refresh_count < 1 and time.monotonic() < deadline:
                t.read(0.03)
            self.assertEqual(server.refresh_count, 1)
            self.assertFalse(self.state()["restore_allowed"])
            t.process.kill()
            t.process.wait(timeout=5)
            # Close the PTY explicitly after the intentionally abrupt exit.
            t.close()
            self.control("Mode", "")
            t = self.terminal()
            self.wait(t, server.url)
            t.read(0.2)
            self.assertIn("URL do servidor", "\n".join(t.screen.display))
            t.close()

    def test_cancel_wallet_unlock_allows_memory_only_login(self):
        self.keyring()
        with HistoryMock() as server:
            t = self.login(server)
            t.close()
            self.control("Mode", "wait")
            before = len(server.requests)
            t = self.terminal()
            self.wait(t, "Recuperando sessão")
            t.send("\x1b")
            t.read(0.2)
            self.assertIn("URL do servidor", "\n".join(t.screen.display))
            self.assertEqual(len(server.requests), before)
            t.send("josé\tSynthetic!123\n")
            self.wait(t, "geral")
            t.close()
            self.assertFalse(self.state()["restore_allowed"])

    def test_failed_public_marker_prevents_refresh_request(self):
        self.keyring()
        with HistoryMock() as server:
            t = self.login(server)
            file = next(Path(self.env["XDG_STATE_HOME"]).glob("papo-tui/session-*.json"))
            file.chmod(0o644)
            t.send("\x1b[17~")
            self.wait(t, "[Renovar]")
            t.send("\x12")
            self.wait(t, "proteger")
            self.assertEqual(server.refresh_count, 0)
            t.close()
            file.chmod(0o600)
            self.assertTrue(self.state()["restore_allowed"])
            t = self.terminal()
            self.wait(t, "geral")
            self.assertEqual(self.login_count(server), 1)
            t.close()

    def test_transient_server_failure_preserves_unrotated_cookie(self):
        self.keyring()
        with HistoryMock() as server:
            t = self.login(server)
            t.close()
            server.api_handler = lambda method, path, body: (503, None)
            t = self.terminal()
            self.wait(t, "Serviço indisponível")
            t.close()
            self.assertTrue(self.state()["restore_allowed"])
            server.api_handler = server.route
            t = self.terminal()
            self.wait(t, "geral")
            self.assertEqual(self.login_count(server), 1)
            t.close()

    def test_unsafe_metadata_is_preserved_without_loading_secrets(self):
        self.keyring()
        with HistoryMock() as server:
            for invalid in ["version", "permissions", "symlink"]:
                t = self.login(server)
                t.close()
                file = next(Path(self.env["XDG_STATE_HOME"]).glob("papo-tui/session-*.json"))
                original = file.read_bytes()
                if invalid == "version":
                    state = json.loads(original)
                    state["version"] = 99
                    file.write_text(json.dumps(state))
                elif invalid == "permissions":
                    file.chmod(0o644)
                else:
                    target = file.with_suffix(".target")
                    file.rename(target)
                    file.symlink_to(target)
                before = len(server.requests)
                counts = self.control("Counts")
                t = self.terminal()
                t.read(0.2)
                self.assertIn("URL do servidor", "\n".join(t.screen.display))
                self.assertEqual(len(server.requests), before)
                self.assertEqual(self.control("Counts"), counts)
                t.close()
                if invalid == "symlink":
                    self.assertTrue(file.is_symlink())
                    file.unlink()
                    target.rename(file)
                elif invalid == "permissions":
                    self.assertEqual(file.stat().st_mode & 0o777, 0o644)
                else:
                    self.assertEqual(json.loads(file.read_bytes())["version"], 99)
                file.chmod(0o600)
                file.write_bytes(original)


if __name__ == "__main__":
    unittest.main()
