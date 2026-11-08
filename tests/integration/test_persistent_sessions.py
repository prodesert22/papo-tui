"""Synthetic HTTP and desktop credentials, exclusively on a private D-Bus."""

from test_context import main as test_main

from test_context import argument

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest

KEYRING = argument(1)
from terminal_support import Terminal  # consumes the TUI binary  # noqa: E402 - path and test arguments are prepared above
from history_mock import HistoryMock  # noqa: E402 - path and test arguments are prepared above
from realtime_mock import RealtimeMock  # noqa: E402 - path and test arguments are prepared above
from r8_mock import R8Mock, AUTHOR, uuid  # noqa: E402 - path and test arguments are prepared above
from history_mock import CHANNEL  # noqa: E402 - path and test arguments are prepared above


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

    def control(self, method, *values):
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
        command.extend(values)
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

    def until(self, t, predicate):
        deadline = time.monotonic() + 8
        while not predicate() and time.monotonic() < deadline:
            t.read(0.03)
        self.assertTrue(predicate())

    def markers(self):
        return sorted(Path(self.env["XDG_STATE_HOME"]).glob("papo-tui/session-*.json"))

    def state(self, url=None):
        """The marker of url, or the only marker of single-server tests."""
        for file in self.markers():
            state = json.loads(file.read_text(encoding="utf-8"))
            if url is None or state.get("url") == url:
                return state
        return {}

    def saved(self, t, url):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            if self.state(url).get("restore_allowed"):
                return
            t.read(0.03)
        self.fail("Credential was not committed; notice=" + "\n".join(t.screen.display))

    def login(self, server):
        server.cookie_max_age = 30 * 86400
        for file in self.markers():
            state = json.loads(file.read_text(encoding="utf-8"))
            state["restore_allowed"] = False
            file.write_text(json.dumps(state))
        t = self.terminal()
        if server.url in "\n".join(t.screen.display):
            t.send("\x1b[Z\x15")
        t.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(t, "geral")
        self.saved(t, server.url)
        return t

    def login_count(self, server):
        return sum(r[1].endswith("/auth/login") for r in server.requests)

    def add_server(self, t, server):
        server.cookie_max_age = 30 * 86400
        row = t.screen.display[0]
        t.click(0, row.index("[+ Adicionar]") + 2)
        self.wait(t, "URL do servidor")
        t.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(t, "[" + server.name + "]")
        self.saved(t, server.url)

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

    def test_every_saved_server_restores_as_a_tab_and_second_window_is_memory_only(self):
        self.keyring()
        with HistoryMock() as first, HistoryMock() as second:
            second.name = "Trabalho"
            t = self.login(first)
            extra = self.terminal()
            self.wait(extra, "URL do servidor")
            self.assertNotIn("geral", "\n".join(extra.screen.display))
            self.assertEqual(self.login_count(first), 1)
            extra.close()
            self.add_server(t, second)
            t.close()
            self.assertEqual(len(self.markers()), 2)
            before = len(first.requests)
            t = self.terminal()
            self.wait(t, "[Trabalho]")
            self.wait(t, "geral")
            row = t.screen.display[0]
            self.assertIn("Comunidade", row, "\n".join(t.screen.display))
            self.assertLess(row.index("Comunidade"), row.index("[Trabalho"))
            self.assertEqual(self.login_count(first), 1)
            self.assertEqual(self.login_count(second), 1)
            restored = first.requests[before:]
            self.assertTrue(any(r[1].endswith("/auth/whoami") for r in restored))
            # The inactive tab validates its session without reading history.
            self.assertFalse(any("/messages" in r[1] for r in restored))
            t.close()

    def test_background_restored_tab_connects_realtime_when_opened(self):
        self.keyring()
        with RealtimeMock() as first, RealtimeMock() as second:
            second.name = "Trabalho"
            t = self.login(first)
            self.add_server(t, second)
            t.close()
            t = self.terminal()
            t.resize(30, 120)
            self.wait(t, "[Trabalho")
            self.wait(t, "Tempo real conectado")
            # The other server validated in the background, where its realtime
            # connection also opens; opening its tab keeps it connected.
            t.click(0, t.screen.display[0].index("Comunidade") + 1)
            self.wait(t, "[Comunidade")
            self.wait(t, "Tempo real conectado")
            message = dict(first.message(4), content="Chegou pelo tempo real")
            first.emit(dict(message, type="message"))
            self.wait(t, message["content"])
            t.close()

    def click_text(self, t, text, columns=25):
        row = next(i for i, line in enumerate(t.screen.display) if text in line[:columns])
        t.click(row, t.screen.display[row].index(text) + 1)

    def test_reopens_the_last_used_server_and_conversation(self):
        self.keyring()
        with HistoryMock() as first, HistoryMock() as second:
            second.name = "Trabalho"
            t = self.login(first)
            self.add_server(t, second)
            self.wait(t, "geral")
            # Use the older server last, on its second channel.
            t.click(0, t.screen.display[0].index("Comunidade") + 1)
            self.wait(t, "[Comunidade]")
            self.wait(t, "# geral · Histórico")
            self.click_text(t, "# outro")
            self.wait(t, "Conteúdo do outro canal")
            self.wait(t, "# outro · Histórico")
            t.close()
            reads = sum("/messages" in r[1] for r in first.requests)
            t = self.terminal()
            # Not the newest server: the one used last, on its last channel.
            self.wait(t, "[Comunidade]")
            self.wait(t, "# outro · Histórico")
            self.wait(t, "Conteúdo do outro canal")
            # Tabs keep the order in which the servers were added.
            row = t.screen.display[0]
            self.assertIn("Trabalho", row)
            self.assertLess(row.index("Comunidade"), row.index("Trabalho"), row)
            self.assertEqual(self.login_count(first), 1)
            # Only the restored channel's history is read, not the first one.
            self.assertEqual(sum("/messages" in r[1] for r in first.requests), reads + 1)
            # The other server keeps its own last channel.
            t.click(0, t.screen.display[0].index("Trabalho") + 1)
            self.wait(t, "[Trabalho")
            self.wait(t, "# geral · Histórico")
            t.close()
            state = json.loads(
                (Path(self.env["XDG_STATE_HOME"]) / "papo-tui" / "view-state.json").read_text()
            )
            self.assertEqual(state["contexts"][-1]["url"], second.url)
            self.assertNotIn("cookie", json.dumps(state).lower())

    def test_unreachable_newest_server_keeps_tabs_to_open_the_other(self):
        self.keyring()
        with HistoryMock() as first, HistoryMock() as second:
            second.name = "Trabalho"
            t = self.login(first)
            self.add_server(t, second)
            t.close()
            second.api_handler = lambda method, path, body: (503, None)
            t = self.terminal()
            # The newest server opens in the form and fails; the other one
            # becomes a tab above the form instead of requiring a new login.
            self.wait(t, "Serviço indisponível")
            self.wait(t, "Comunidade")
            row = next(i for i, line in enumerate(t.screen.display) if "Comunidade" in line)
            self.assertEqual(row, 0)
            self.assertIn("URL do servidor", "\n".join(t.screen.display))
            t.click(row, t.screen.display[row].index("Comunidade") + 1)
            self.wait(t, "[Comunidade]")
            self.wait(t, "geral")
            self.assertNotIn("URL do servidor", "\n".join(t.screen.display))
            self.assertEqual(self.login_count(first), 1)
            # The unreachable server stays listed with its saved session.
            self.wait(t, "(offline)")
            self.assertIn("(offline)", t.screen.display[0])
            self.assertTrue(self.state(second.url)["restore_allowed"])
            # Opening it again reads the saved session instead of asking a password.
            second.api_handler = second.route
            t.click(0, t.screen.display[0].index("(offline)"))
            self.wait(t, "[Trabalho]")
            self.wait(t, "geral")
            self.assertNotIn("(offline)", t.screen.display[0])
            self.assertIn("Comunidade", t.screen.display[0])
            self.assertEqual(self.login_count(second), 1)
            t.close()

    def test_unreachable_background_server_restores_as_offline_tab(self):
        self.keyring()
        with HistoryMock() as first, HistoryMock() as second:
            second.name = "Trabalho"
            t = self.login(first)
            self.add_server(t, second)
            t.close()
            first.api_handler = lambda method, path, body: (503, None)
            t = self.terminal()
            self.wait(t, "[Trabalho]")
            self.wait(t, "geral")
            self.wait(t, "(offline)")
            row = t.screen.display[0]
            self.assertIn("(offline)", row, "\n".join(t.screen.display))
            self.assertLess(row.index("(offline)"), row.index("[Trabalho"))
            self.assertTrue(self.state(first.url)["restore_allowed"])
            t.close()

    def test_offline_background_tab_reconnects_when_its_server_returns(self):
        self.keyring()
        with HistoryMock() as first, HistoryMock() as second:
            second.name = "Trabalho"
            t = self.login(first)
            self.add_server(t, second)
            t.close()
            first.api_handler = lambda method, path, body: (503, None)
            t = self.terminal()
            self.wait(t, "[Trabalho]")
            self.wait(t, "geral")
            self.wait(t, "(offline)")
            # While it stays down, it is retried in the background without
            # leaving the open conversation or asking for a password.
            attempts = len(first.requests)
            self.until(t, lambda: len(first.requests) > attempts)
            self.assertIn("(offline)", t.screen.display[0])
            first.api_handler = first.route
            # After one failed retry the next comes 8 s (±20%) later.
            deadline = time.monotonic() + 15
            while "(offline)" in t.screen.display[0] and time.monotonic() < deadline:
                t.read(0.05)
            self.assertNotIn("(offline)", t.screen.display[0])
            self.assertIn("Comunidade", t.screen.display[0])
            self.assertIn("[Trabalho]", t.screen.display[0])
            self.assertIn("Conversar em #geral", "\n".join(t.screen.display))
            self.assertEqual(self.login_count(first), 1)
            t.close()

    def test_unreachable_saved_server_opens_by_itself_when_it_returns(self):
        self.keyring()
        with HistoryMock() as server:
            t = self.login(server)
            t.close()
            server.api_handler = lambda method, path, body: (503, None)
            t = self.terminal()
            self.wait(t, "Serviço indisponível")
            self.assertIn("URL do servidor", "\n".join(t.screen.display))
            server.api_handler = server.route
            # The saved session is read again on its own; no click or password.
            self.wait(t, "Conversar em #geral")
            self.assertNotIn("URL do servidor", "\n".join(t.screen.display))
            self.assertEqual(self.login_count(server), 1)
            t.close()

    def test_mentions_arrive_after_an_unreachable_saved_server_returns(self):
        self.keyring()
        with R8Mock(count=0) as server:
            t = self.login(server)
            t.close()
            # The backend is stopped: every connection is refused.
            server.power_off()
            t = self.terminal()
            self.wait(t, "Conexão recusada")
            server.power_on()
            self.wait(t, "Conversar em #geral")
            self.wait(t, "Tempo real conectado")
            content = "Olá @mention(<@" + uuid(1) + ">)"
            mention = dict(server.message(50), author_id=AUTHOR, content=content)
            server.emit(dict(mention, type="message"))
            server.notifications = [
                dict(
                    id=uuid(9060),
                    message_id=mention["id"],
                    channel_id=CHANNEL,
                    author_id=AUTHOR,
                    message_content=content,
                    read=False,
                    created_at=mention["created_at"],
                )
            ]
            server.emit(
                dict(
                    type="new_notification",
                    id=uuid(9060),
                    user_id=AUTHOR,
                    message_id=mention["id"],
                    message_content=content,
                )
            )
            self.wait(t, "[Comunidade 1]")
            self.wait(t, "1 notificação")
            t.close()

    def test_mentions_arrive_after_an_offline_background_server_returns(self):
        self.keyring()
        with R8Mock(count=0) as first, HistoryMock() as second:
            second.name = "Trabalho"
            t = self.login(first)
            self.add_server(t, second)
            t.close()
            # The backend is stopped: every connection is refused.
            first.power_off()
            t = self.terminal()
            self.wait(t, "[Trabalho]")
            self.wait(t, "(offline)")
            first.power_on()
            deadline = time.monotonic() + 15
            while "(offline)" in t.screen.display[0] and time.monotonic() < deadline:
                t.read(0.05)
            self.assertNotIn("(offline)", t.screen.display[0])
            # Its realtime connection opens in the background.
            deadline = time.monotonic() + 10
            while not first.peers and time.monotonic() < deadline:
                t.read(0.05)
            self.assertTrue(first.peers, "background realtime did not reconnect")
            content = "Olá @mention(<@" + uuid(1) + ">)"
            mention = dict(first.message(50), author_id=AUTHOR, content=content)
            first.emit(dict(mention, type="message"))
            first.notifications = [
                dict(
                    id=uuid(9060),
                    message_id=mention["id"],
                    channel_id=CHANNEL,
                    author_id=AUTHOR,
                    message_content=content,
                    read=False,
                    created_at=mention["created_at"],
                )
            ]
            first.emit(
                dict(
                    type="new_notification",
                    id=uuid(9060),
                    user_id=AUTHOR,
                    message_id=mention["id"],
                    message_content=content,
                )
            )
            self.wait(t, "Comunidade 1")
            t.close()

    def test_saved_server_tabs_show_with_the_login_before_validation(self):
        self.keyring()
        with HistoryMock() as first, HistoryMock() as second:
            second.name = "Trabalho"
            t = self.login(first)
            self.add_server(t, second)
            t.close()
            release = threading.Event()
            route = second.route

            def slow(method, path, body):
                if path.endswith("/auth/whoami"):
                    release.wait(5)
                return route(method, path, body)

            second.api_handler = slow
            try:
                t = self.terminal()
                # The newest server is still confirming its session, yet the
                # other saved server is already a tab above the login form.
                self.wait(t, "Comunidade")
                self.assertIn("Comunidade", t.screen.display[0])
                self.wait(t, "Conectando")
                self.assertIn("URL do servidor", "\n".join(t.screen.display))
            finally:
                release.set()
            self.wait(t, "geral")
            self.wait(t, "[Trabalho")
            self.assertIn("Comunidade", t.screen.display[0])
            self.assertEqual(self.login_count(second), 1)
            t.close()

    def right_click(self, t, row, column):
        t.send(f"\x1b[<2;{column + 1};{row + 1}M\x1b[<2;{column + 1};{row + 1}m")

    def deletes(self):
        return int(self.control("Counts").strip("()\n").split(",")[2].strip().split()[-1])

    def test_offline_server_is_removed_with_its_saved_session(self):
        self.keyring()
        with HistoryMock() as first, HistoryMock() as second:
            second.name = "Trabalho"
            t = self.login(first)
            self.add_server(t, second)
            t.close()
            second.api_handler = lambda method, path, body: (503, None)
            t = self.terminal()
            self.wait(t, "Serviço indisponível")
            self.wait(t, "Comunidade")
            t.click(0, t.screen.display[0].index("Comunidade") + 1)
            self.wait(t, "(offline)")
            deletes = self.deletes()
            # A right-click opens the tab's menu under it, not the confirmation.
            self.right_click(t, 0, t.screen.display[0].index("(offline)"))
            self.until(t, lambda: "Remover servidor" in t.screen.display[2])
            self.assertNotIn("Remover servidor?", "\n".join(t.screen.display))
            t.send("\x1b")  # Esc closes the menu.
            self.until(t, lambda: "Remover servidor" not in "\n".join(t.screen.display))
            self.right_click(t, 0, t.screen.display[0].index("(offline)"))
            self.until(t, lambda: "Remover servidor" in t.screen.display[2])
            t.send("\n")
            self.wait(t, "Remover servidor?")
            t.send("\x1b")  # Esc keeps the server.
            t.read(0.3)
            self.assertNotIn("Remover servidor?", "\n".join(t.screen.display))
            self.assertIn("(offline)", t.screen.display[0])
            self.right_click(t, 0, t.screen.display[0].index("(offline)"))
            self.until(t, lambda: "Remover servidor" in t.screen.display[2])
            t.click(2, t.screen.display[2].index("Remover servidor"))
            self.wait(t, "Remover servidor?")
            t.send("\x1b[C\n")
            deadline = time.monotonic() + 8
            while "(offline)" in t.screen.display[0] and time.monotonic() < deadline:
                t.read(0.05)
            self.assertNotIn("(offline)", t.screen.display[0])
            self.assertIn("[Comunidade]", t.screen.display[0])
            while self.deletes() == deletes and time.monotonic() < deadline:
                t.read(0.05)
            self.assertGreater(self.deletes(), deletes)
            self.assertEqual(self.state(second.url), {})
            t.close()
            second.api_handler = second.route
            t = self.terminal()
            self.wait(t, "[Comunidade]")
            self.wait(t, "geral")
            self.assertNotIn("127.0.0.1", t.screen.display[0])
            self.assertEqual(self.login_count(second), 1)
            t.close()

    def test_online_server_removed_by_keyboard_from_the_tabs(self):
        self.keyring()
        with HistoryMock() as first, HistoryMock() as second:
            second.name = "Trabalho"
            t = self.login(first)
            self.add_server(t, second)
            self.wait(t, "geral")
            t.send("\x1b\x1b\x1bOA")  # Esc to the channel list, then up to the tabs.
            deadline = time.monotonic() + 8
            while "Foco: Abas" not in "\n".join(t.screen.display) and time.monotonic() < deadline:
                t.send("\x1bOA")
                t.read(0.1)
            self.wait(t, "Foco: Abas")
            t.send("\x1b[3~")  # Delete on the selected Trabalho tab.
            self.wait(t, "Remover servidor?")
            self.wait(t, "Trabalho")
            t.send("\t\n")
            self.wait(t, "[Comunidade]")
            self.wait(t, "geral")
            self.assertNotIn("Trabalho", t.screen.display[0])
            self.assertEqual(self.state(second.url), {})
            self.assertTrue(self.state(first.url)["restore_allowed"])
            t.close()
            t = self.terminal()
            self.wait(t, "[Comunidade]")
            self.wait(t, "geral")
            self.assertNotIn("Trabalho", t.screen.display[0])
            self.assertEqual(self.login_count(first), 1)
            t.close()

    def test_online_server_removed_from_its_tab_context_menu(self):
        self.keyring()
        with HistoryMock() as first, HistoryMock() as second:
            second.name = "Trabalho"
            t = self.login(first)
            self.add_server(t, second)
            self.wait(t, "geral")
            self.wait(t, "[Trabalho")
            column = t.screen.display[0].index("Trabalho")
            # A right-click opens the tab's menu under it, titled with the server.
            # The server in use also offers its settings, above Remover servidor.
            self.right_click(t, 0, column)
            self.until(t, lambda: "Remover servidor" in t.screen.display[3])
            self.assertIn("Configurações do servidor", t.screen.display[2])
            self.assertIn("Trabalho", t.screen.display[1])
            self.assertNotIn("Remover servidor?", "\n".join(t.screen.display))
            t.click(10, 50)  # A click elsewhere closes it and keeps the server.
            self.until(t, lambda: "Remover servidor" not in "\n".join(t.screen.display))
            self.assertIn("Trabalho", t.screen.display[0])
            # m on the focused tab opens the same menu from the keyboard.
            self.right_click(t, 0, column)
            self.until(t, lambda: "Remover servidor" in t.screen.display[3])
            t.send("m")
            self.until(t, lambda: "Remover servidor" not in "\n".join(t.screen.display))
            t.send("m")
            self.until(t, lambda: "Remover servidor" in t.screen.display[3])
            t.click(3, t.screen.display[3].index("Remover servidor"))
            self.wait(t, "Remover servidor?")
            t.send("\t\n")
            self.wait(t, "[Comunidade]")
            self.wait(t, "geral")
            self.assertNotIn("Trabalho", t.screen.display[0])
            self.assertEqual(self.state(second.url), {})
            t.close()

    def test_unreachable_form_server_is_removed_from_the_login_tabs(self):
        self.keyring()
        with HistoryMock() as first, HistoryMock() as second:
            second.name = "Trabalho"
            t = self.login(first)
            self.add_server(t, second)
            t.close()
            second.api_handler = lambda method, path, body: (503, None)
            t = self.terminal()
            self.wait(t, "Serviço indisponível")
            self.wait(t, "Comunidade")
            t.send("\x1b[A" * 6)  # Up from the first field reaches the tabs.
            t.send("\x1b[3~")
            self.wait(t, "Remover servidor?")
            self.wait(t, "127.0.0.1")
            t.send("\x1b[C\n")
            self.wait(t, "[Comunidade]")
            self.wait(t, "geral")
            self.assertEqual(self.state(second.url), {})
            t.close()
            t = self.terminal()
            self.wait(t, "[Comunidade]")
            self.wait(t, "geral")
            self.assertEqual(self.login_count(first), 1)
            t.close()

    def test_login_tabs_switch_servers_by_keyboard(self):
        self.keyring()
        with HistoryMock() as first, HistoryMock() as second:
            second.name = "Trabalho"
            t = self.login(first)
            self.add_server(t, second)
            t.close()
            second.api_handler = lambda method, path, body: (503, None)
            t = self.terminal()
            self.wait(t, "Serviço indisponível")
            self.wait(t, "Comunidade")
            t.send("\x1b[A" * 6)  # Up from the first field reaches the tabs.
            self.wait(t, "[127.0.0.1")
            # Back in the URL field, the terminal cursor is inside the field,
            # not left on the tab bar drawn above the card.
            t.send("\x1b[B")
            deadline = time.monotonic() + 8
            while time.monotonic() < deadline and (
                t.screen.cursor.hidden or second.url not in t.screen.display[t.screen.cursor.y]
            ):
                t.read(0.03)
            self.assertFalse(t.screen.cursor.hidden)
            self.assertIn(second.url, t.screen.display[t.screen.cursor.y])
            t.send("\x1b[A")
            t.read(0.2)
            self.assertTrue(t.screen.cursor.hidden)
            t.send("\x1b[D")
            self.wait(t, ">Comunidade<")
            t.send("\x1b[C")
            self.wait(t, "[127.0.0.1")
            self.assertNotIn(">Comunidade<", "\n".join(t.screen.display))
            t.send("\x1b[D\n")
            self.wait(t, "[Comunidade]")
            self.wait(t, "geral")
            self.assertEqual(self.login_count(first), 1)
            t.close()

    @unittest.skipUnless(os.environ.get("PAPO_TEST_SECRET"), "libsecret adapter not built")
    def test_secret_service_restores_every_server(self):
        self.keyring("secret")
        with HistoryMock() as first, HistoryMock() as second:
            second.name = "Trabalho"
            t = self.login(first)
            self.add_server(t, second)
            t.close()
            t = self.terminal()
            self.wait(t, "[Trabalho]")
            self.wait(t, "geral")
            self.assertIn("Comunidade", t.screen.display[0])
            self.assertEqual(self.login_count(first), 1)
            self.assertEqual(self.login_count(second), 1)
            t.close()

    def test_logout_forgets_only_that_server(self):
        self.keyring()
        with HistoryMock() as first, HistoryMock() as second:
            second.name = "Trabalho"
            t = self.login(first)
            self.add_server(t, second)
            self.wait(t, "geral")
            t.send("\x0c")
            self.wait(t, "Você saiu da conta")
            t.close()
            self.assertFalse(self.state(second.url)["restore_allowed"])
            self.assertTrue(self.state(first.url)["restore_allowed"])
            t = self.terminal()
            self.wait(t, "[Comunidade]")
            self.wait(t, "geral")
            self.assertNotIn("Trabalho", t.screen.display[0])
            self.assertEqual(self.login_count(first), 1)
            self.assertEqual(self.login_count(second), 1)
            t.close()

    def test_session_saved_before_multiple_servers_restores_without_login(self):
        self.keyring()
        with HistoryMock() as server:
            t = self.login(server)
            t.close()
            (marker,) = self.markers()
            key = marker.stem.removeprefix("session-")
            scope, sequence = key.rsplit("-", 1)
            self.assertEqual(sequence, "1")
            # Recreate the names of the former single-session store.
            legacy = marker.with_name("session-" + scope + ".json")
            marker.rename(legacy)
            self.control("Rekey", '"' + key + '"', '"' + scope + '"')
            t = self.terminal()
            self.wait(t, "geral")
            self.assertEqual(self.login_count(server), 1)
            t.read(0.2)
            t.close()
            self.assertEqual(self.markers(), [legacy])
            self.assertTrue(self.state()["restore_allowed"])

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
            t.send("\x14")
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

    def test_quitting_during_a_renewal_keeps_the_saved_server(self):
        self.keyring()
        with HistoryMock() as server:
            t = self.login(server)
            server.scenario = "refresh-slow"  # The reply takes 1.5s.
            t.send("\x14")
            self.wait(t, "[Renovar]")
            t.send("\x12")
            deadline = time.monotonic() + 5
            while server.refresh_count < 1 and time.monotonic() < deadline:
                t.read(0.03)
            self.assertEqual(server.refresh_count, 1)
            # The renewal has withdrawn the saved session until its new
            # cookie is stored; quitting now must wait for it.
            self.assertFalse(self.state()["restore_allowed"])
            t.send("\x11")
            t.process.wait(timeout=10)
            t.close()
            self.assertTrue(self.state()["restore_allowed"])
            server.scenario = ""
            t = self.terminal()
            self.wait(t, "geral")
            self.assertEqual(self.login_count(server), 1)
            self.assertTrue(
                any(
                    r[2] == "Auth=rotated-fixture-1"
                    for r in server.requests
                    if r[1].endswith("/auth/whoami")
                )
            )
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
            t.send("\x14")
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
    test_main(
        2,
    )
