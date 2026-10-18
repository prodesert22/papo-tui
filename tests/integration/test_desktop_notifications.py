"""Desktop notification behavior on a private D-Bus, never the personal daemon."""

import json
import os
import select
import subprocess
import sys
import time
import unittest

FAKE = sys.argv.pop(2)
from r8_mock import R8Mock, AUTHOR, uuid  # noqa: E402 - path and test arguments are prepared above
from test_terminal import Terminal  # noqa: E402 - path and test arguments are prepared above


class DesktopTests(unittest.TestCase):
    def setUp(self):
        self.daemon = subprocess.Popen([FAKE], stdout=subprocess.PIPE, text=True)
        self.addCleanup(self.stop)
        self.assertEqual(self.daemon.stdout.readline().strip(), "ready")

    def stop(self):
        self.daemon.terminate()
        self.daemon.wait(timeout=5)
        self.daemon.stdout.close()

    def login(self, server):
        t = Terminal(
            environment={"DBUS_SESSION_BUS_ADDRESS": os.environ["DBUS_SESSION_BUS_ADDRESS"]}
        )
        self.addCleanup(lambda: t.close() if t.process.poll() is None else None)
        t.send(server.url + "\tjosé\tSynthetic!123\n")
        t.wait_text("Tempo real conectado")
        t.wait_text("Categoria")
        return t

    def event(self, n=1, content="<b>Olá</b>\x1b]52;c;untrusted\x07"):
        return dict(
            type="new_notification",
            id=uuid(9000 + n),
            user_id=AUTHOR,
            message_id=uuid(9500 + n),
            message_content=content,
        )

    def receive(self, t):
        end = time.monotonic() + 6
        while time.monotonic() < end:
            t.read(0.03)
            if select.select([self.daemon.stdout], [], [], 0)[0]:
                return json.loads(self.daemon.stdout.readline())
        self.fail("No desktop notification")

    def test_ephemeral_grouping_markup_and_no_actions_or_remote_read(self):
        with R8Mock(count=0) as server:
            t = self.login(server)
            t.send("\x1b[O")
            server.emit(self.event())
            server.emit(self.event(2))
            server.emit(self.event(2))
            value = self.receive(t)
            self.assertIn("&lt;b&gt;Olá&lt;/b&gt;", value["body"])
            self.assertIn("2 novos avisos", value["body"])
            self.assertNotIn("\x1b", value["body"])
            self.assertNotIn("\x07", value["body"])
            self.assertEqual(value["actions"], 0)
            self.assertEqual(server.marked, [])
            t.read(0.4)
            self.assertFalse(select.select([self.daemon.stdout], [], [], 0)[0])

    def test_visible_conversation_suppresses_and_focus_loss_emits(self):
        with R8Mock(count=0) as server:
            t = self.login(server)
            t.send("\x1bOB\n")
            t.wait_text("Mensagem 0003")
            t.send("\x1b[I")
            message = server.message(4)
            server.emit(dict(message, type="message"))
            event = self.event()
            event["message_id"] = message["id"]
            server.emit(event)
            t.read(2.5)
            self.assertFalse(select.select([self.daemon.stdout], [], [], 0)[0])
            t.send("\x1b[O")
            message = server.message(5)
            server.emit(dict(message, type="message"))
            event = self.event(2)
            event["message_id"] = message["id"]
            server.emit(event)
            self.receive(t)
            self.assertEqual(server.marked, [])

    def test_mentions_use_usernames_instead_of_tokens_or_nicknames(self):
        with R8Mock(count=0) as server:
            parent = server.api_handler

            def route(method, path, body):
                response = parent(method, path, body)
                if path.endswith("/users/user_summary_batch") and response and response[0] == 200:
                    for user in response[1]:
                        if user["id"] == uuid(21):
                            user.update(username="élise", nickname="Apelido")
                return response

            server.api_handler = route
            t = self.login(server)
            t.resize(32, 120)
            server.emit(
                dict(type="presence_update", user_id=uuid(21), status="online", nickname="Apelido")
            )
            t.wait_text("Apelido")
            before = sum("/messages" in path for _, path, *_ in server.requests)
            t.send("\x1b[O")
            content = (
                f"Olá @mention(<@{uuid(1)}>), @mention(<@{uuid(21)}>), "
                f"@mention(<@{uuid(22)}>) e @everyone; de novo @mention(<@{uuid(21)}>)."
            )
            server.emit(self.event(content=content))
            value = self.receive(t)
            self.assertEqual(
                value["body"],
                "Olá @josé, @élise, @usuário indisponível e @everyone; de novo @élise.",
            )
            self.assertNotIn("@mention(", value["body"])
            self.assertNotIn("Apelido", value["body"])
            self.assertEqual(server.marked, [])
            self.assertEqual(sum("/messages" in path for _, path, *_ in server.requests), before)

    def test_disabled_and_hidden_preview_preferences(self):
        with R8Mock(count=0) as server:
            t = self.login(server)
            t.send("\x1b[20~\t\n")
            t.wait_text("Preferências")
            t.send("\t\n\x1b\x1b\x1b[O")
            server.emit(self.event(content="conteúdo privado"))
            value = self.receive(t)
            self.assertNotIn("conteúdo privado", value["body"])
            t.send("\x1b[20~\t\n\n\x1b\x1b")
            server.emit(self.event(2))
            t.read(2.5)
            self.assertFalse(select.select([self.daemon.stdout], [], [], 0)[0])

    def test_slow_daemon_keeps_tui_responsive_and_pending_exit_is_safe(self):
        self.stop()
        self.daemon = subprocess.Popen(
            [FAKE],
            stdout=subprocess.PIPE,
            text=True,
            env=dict(os.environ, PAPO_NOTIFY_TEST_DELAY="4000"),
        )
        self.assertEqual(self.daemon.stdout.readline().strip(), "ready")
        with R8Mock(count=0) as server:
            t = self.login(server)
            server.emit(self.event())
            self.receive(t)
            t.send("\x1b[20~")
            t.wait_text("Notificações")
            start = time.monotonic()
            self.assertTrue(t.close())
            self.assertLess(time.monotonic() - start, 2)

    def test_daemon_restart_resumes_only_new_events(self):
        with R8Mock(count=0) as server:
            t = self.login(server)
            server.emit(self.event())
            self.receive(t)
            self.stop()
            t.read(0.2)
            self.daemon = subprocess.Popen([FAKE], stdout=subprocess.PIPE, text=True)
            self.assertEqual(self.daemon.stdout.readline().strip(), "ready")
            t.read(0.2)
            self.assertFalse(select.select([self.daemon.stdout], [], [], 0)[0])
            server.emit(self.event(2))
            self.receive(t)


if __name__ == "__main__":
    unittest.main()
