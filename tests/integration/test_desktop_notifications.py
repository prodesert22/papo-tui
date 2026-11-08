"""Desktop notification behavior on a private D-Bus, never the personal daemon."""

from test_context import main as test_main

from test_context import argument

import base64
import json
import os
import re
import select
import subprocess
import tempfile
import time
import unittest

FAKE = argument(1)
from r8_mock import R8Mock, AUTHOR, uuid  # noqa: E402 - path and test arguments are prepared above
from channel_preferences import ChannelPreferencesMock  # noqa: E402 - shared fixture
from terminal_support import Terminal, wait_for_screen  # noqa: E402 - path and test arguments are prepared above
from window_managers import Hyprland, Sway  # noqa: E402 - shared fixture


class DesktopTests(unittest.TestCase):
    def setUp(self):
        self.daemon = subprocess.Popen([FAKE], stdout=subprocess.PIPE, text=True)
        self.addCleanup(self.stop)
        self.assertEqual(self.daemon.stdout.readline().strip(), "ready")

    def stop_daemon(self):
        self.daemon.terminate()
        self.daemon.wait(timeout=5)
        self.daemon.stdout.close()

    def stop(self):
        if self.daemon.poll() is not None:
            return
        self.daemon.terminate()
        self.daemon.wait(timeout=5)
        self.daemon.stdout.close()

    def start_daemon(self, **environment):
        self.stop_daemon()
        self.daemon = subprocess.Popen(
            [FAKE], stdout=subprocess.PIPE, text=True, env=dict(os.environ, **environment)
        )
        self.assertEqual(self.daemon.stdout.readline().strip(), "ready")

    def login(self, server, terminal_actions=None, **environment):
        # Focus helpers such as the KWin script write files to the runtime
        # directory; keep them out of the user's session, which may also be
        # read-only in a jail. Tests that need a specific one pass their own.
        runtime = tempfile.TemporaryDirectory(prefix="papo-test-runtime-")
        self.addCleanup(runtime.cleanup)
        os.chmod(runtime.name, 0o700)
        t = Terminal(
            environment={
                "DBUS_SESSION_BUS_ADDRESS": os.environ["DBUS_SESSION_BUS_ADDRESS"],
                "XDG_RUNTIME_DIR": runtime.name,
                **environment,
            }
        )
        self.addCleanup(lambda: t.close() if t.process.poll() is None else None)
        if terminal_actions is not None:
            # Answer the OSC 99 query like Kitty does, with these click actions.
            probe = rb"\x1b\]99;i=(papo-[0-9a-f]{8}-)probe:p=\?;\x1b\\"
            wait_for_screen(t, lambda: re.search(probe, t.output), "OSC 99 query")
            self.prefix = re.search(probe, t.output)[1].decode()
            t.send(
                f"\x1b]99;i={self.prefix}probe:p=?;a={terminal_actions}:o=always,unfocused,"
                "invisible:u=0,1,2:p=title,body,?,close,icon,alive,buttons:c=1:w=1:"
                "s=system,silent,error,info,question,warn,warning\x1b\\"
            )
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
            # One default action ("default" and its label) opens the conversation
            # when the notification is clicked; nothing else is offered.
            self.assertEqual(value["actions"], 2)
            self.assertEqual(server.marked, [])
            t.read(0.4)
            self.assertFalse(select.select([self.daemon.stdout], [], [], 0)[0])

    def test_clicking_the_notification_opens_its_conversation_and_message(self):
        from history_mock import OTHER

        self.stop_daemon()
        environment = dict(os.environ, PAPO_NOTIFY_TEST_CLICK="600")
        self.daemon = subprocess.Popen([FAKE], stdout=subprocess.PIPE, text=True, env=environment)
        self.assertEqual(self.daemon.stdout.readline().strip(), "ready")
        with R8Mock(count=3) as server:
            t = self.login(server)
            t.send("\x1bOB\n")
            t.wait_text("Mensagem 0003")
            t.send("\x1b[O")
            message = dict(server.message(1), id=uuid(777), channel_id=OTHER, content="Chegou agora")
            server.emit(dict(message, type="message"))
            event = self.event()
            event["message_id"] = message["id"]
            server.emit(event)
            self.receive(t)
            # The daemon then reports a click on that notification.
            t.wait_text("Conteúdo do outro canal")
            self.assertEqual(server.marked, [])

    def test_clicking_a_notification_of_another_server_opens_its_tab(self):
        from history_mock import OTHER

        self.start_daemon(PAPO_NOTIFY_TEST_CLICK="600")
        with R8Mock(count=3) as first, R8Mock(count=0) as second:
            second.name = "Trabalho"
            t = self.login(first)
            t.click(0, t.screen.display[0].index("[+ Adicionar]") + 2)
            t.wait_text("URL do servidor")
            t.send(second.url + "\tjosé\tSynthetic!123\n")
            wait_for_screen(t, lambda: "[Trabalho" in t.screen.display[0], "second server")
            t.wait_text("Tempo real conectado")
            t.send("\x1b[O")
            message = dict(first.message(1), id=uuid(777), channel_id=OTHER, content="Chegou agora")
            first.emit(dict(message, type="message"))
            event = self.event()
            event["message_id"] = message["id"]
            first.emit(event)
            self.receive(t)
            # The click switches to the notification's server and opens it.
            wait_for_screen(
                t, lambda: "[Comunidade" in t.screen.display[0], "the clicked server's tab"
            )
            t.wait_text("Conteúdo do outro canal")
            self.assertEqual(first.marked, [])

    def terminal_notification(self, t):
        pattern = (
            rb"\x1b\]99;i="
            + re.escape(self.prefix.encode())
            + rb"([0-9]+):([^;]*);([^\x1b]*)\x1b\\"
        )
        wait_for_screen(
            t, lambda: len(re.findall(pattern, t.output)) >= 2, "OSC 99 notification", seconds=8
        )
        (first, title_meta, title), (second, body_meta, body) = re.findall(pattern, t.output)[:2]
        self.assertEqual(first, second)
        self.assertIn(b"a=focus,report", title_meta)
        self.assertIn(b"d=0", title_meta)
        self.assertIn(b"p=title", title_meta)
        self.assertIn(b"e=1", title_meta)
        self.assertIn(b"p=body", body_meta)
        return first.decode(), base64.b64decode(title).decode(), base64.b64decode(body).decode()

    def test_terminal_notification_focuses_its_tab_and_opens_the_message(self):
        from history_mock import OTHER

        self.start_daemon(PAPO_NOTIFY_TEST_DESKTOP="1")
        with R8Mock(count=3) as server:
            t = self.login(server, terminal_actions="focus,report", XDG_CURRENT_DESKTOP="KDE")
            t.send("\x1bOB\n")
            t.wait_text("Mensagem 0003")
            t.send("\x1b[O")
            message = dict(
                server.message(1), id=uuid(777), channel_id=OTHER, content="Chegou agora"
            )
            server.emit(dict(message, type="message"))
            event = self.event()
            event["message_id"] = message["id"]
            server.emit(event)
            identifier, title, body = self.terminal_notification(t)
            self.assertIn("·", title)
            self.assertIn("Olá", body)
            for control in ("\x1b", "\x07", "\n"):
                self.assertNotIn(control, title + body)
            # The terminal shows it instead of a second, separate D-Bus popup,
            # and nothing opens before the click.
            t.read(1)
            self.assertFalse(select.select([self.daemon.stdout], [], [], 0)[0])
            self.assertNotIn(b"Conte\xc3\xbado do outro canal", t.output)
            # The terminal focuses the tab, then reports the click.
            t.send(f"\x1b]99;i={self.prefix}{identifier};\x1b\\")
            t.wait_text("Conteúdo do outro canal")
            # The terminal already raised itself: no window manager request.
            t.read(1)
            self.assertFalse(select.select([self.daemon.stdout], [], [], 0)[0])
            screen = "\n".join(t.screen.display)
            self.assertNotIn("99;", screen)
            self.assertNotIn("probe", screen)
            self.assertEqual(server.marked, [])

    def test_terminal_without_click_reports_keeps_the_desktop_notification(self):
        with R8Mock(count=0) as server:
            t = self.login(server, terminal_actions="focus")
            t.send("\x1b[O")
            server.emit(self.event())
            self.receive(t)
            self.assertNotRegex(bytes(t.output), rb"\x1b\]99;i=papo-[0-9a-f]{8}-[0-9]")

    def notify_other_channel(self, server, t):
        from history_mock import OTHER

        t.send("\x1bOB\n")
        t.wait_text("Mensagem 0003")
        t.send("\x1b[O")
        message = dict(server.message(1), id=uuid(777), channel_id=OTHER, content="Chegou agora")
        server.emit(dict(message, type="message"))
        event = self.event()
        event["message_id"] = message["id"]
        server.emit(event)
        self.receive(t)

    def records(self, t, keys, seconds=6):
        """Daemon records holding each key, in arrival order."""
        found = []
        end = time.monotonic() + seconds
        while time.monotonic() < end and len(found) < len(keys):
            t.read(0.03)
            while select.select([self.daemon.stdout], [], [], 0)[0]:
                record = json.loads(self.daemon.stdout.readline())
                found += [(key, record) for key in keys if key in record]
        self.assertEqual([key for key, _ in found], keys)
        return dict(found)

    def wait_commands(self, t, manager, description):
        wait_for_screen(t, lambda: manager.commands, description)
        t.read(0.3)
        return manager.commands

    def fake_tmux(self, directory, client_pid):
        log = os.path.join(directory, "tmux.log")
        tmux = os.path.join(directory, "tmux")
        with open(tmux, "w") as script:
            script.write(
                "#!/bin/sh\n"
                f"printf '%s\\n' \"$*\" >> '{log}'\n"
                'case "$1" in list-clients)\n'
                "  echo '5 1'\n"
                f"  echo '9 {client_pid}'\n"
                "esac\n"
            )
        os.chmod(tmux, 0o755)
        return log

    def test_click_selects_the_konsole_tab_and_activates_the_window_through_kwin(self):
        self.start_daemon(PAPO_NOTIFY_TEST_CLICK="300", PAPO_NOTIFY_TEST_DESKTOP="1")
        with R8Mock(count=3) as server:
            t = self.login(
                server,
                XDG_CURRENT_DESKTOP="KDE",
                KONSOLE_DBUS_SERVICE="org.kde.konsole-test",
                KONSOLE_DBUS_WINDOW="/Windows/1",
                KONSOLE_DBUS_SESSION="/Sessions/7",
            )
            self.notify_other_channel(server, t)
            found = self.records(t, ["konsole_session", "kwin_script", "kwin_run", "kwin_unload"])
            t.wait_text("Conteúdo do outro canal")
            # The tab is selected first, so the window title already follows it.
            self.assertEqual(found["konsole_session"]["konsole_session"], 7)
            self.assertEqual(found["konsole_session"]["window"], "/Windows/1")
            script = found["kwin_script"]["kwin_script"]
            arguments = re.search(r"\}\)\((\[[0-9,]*\]), (.*)\);\s*$", script)
            pids, title = json.loads(arguments[1]), json.loads(arguments[2])
            # Nearest process first: Papo TUI, then this test that started it.
            self.assertEqual(pids[:2], [t.process.pid, os.getpid()])
            self.assertTrue(title)
            self.assertIn(f"\x1b]2;{title}\x1b\\".encode(), t.output)
            self.assertIn("workspace.activeWindow = best", script)
            self.assertEqual(found["kwin_run"]["kwin_run"], "/Scripting/Script0")
            self.assertEqual(found["kwin_unload"]["kwin_unload"], found["kwin_script"]["plugin"])
            self.assertEqual(server.marked, [])

    def test_click_selects_the_tmux_pane_and_focuses_the_client_window_in_sway(self):
        directory = tempfile.TemporaryDirectory(prefix="papo-focus-")
        self.addCleanup(directory.cleanup)
        # The newest tmux client is this test process; its window is con 42.
        log = self.fake_tmux(directory.name, os.getpid())
        windows = [
            {"id": 41, "pid": 1, "name": "other", "nodes": []},
            {"id": 42, "pid": os.getpid(), "name": "terminal", "nodes": []},
        ]
        self.start_daemon(PAPO_NOTIFY_TEST_CLICK="300")
        with Sway(os.path.join(directory.name, "sway.sock"), windows) as sway:
            with R8Mock(count=3) as server:
                t = self.login(
                    server,
                    TMUX="/tmp/papo-fake-tmux,1,0",
                    TMUX_PANE="%3",
                    PATH=directory.name + os.pathsep + os.environ["PATH"],
                    SWAYSOCK=sway.path,
                )
                self.notify_other_channel(server, t)
                commands = self.wait_commands(t, sway, "sway focus")
                t.wait_text("Conteúdo do outro canal")
        self.assertEqual(commands, ["[con_id=42] focus"])
        with open(log) as calls:
            self.assertEqual(
                calls.read().splitlines(),
                [
                    "select-window -t %3 ; select-pane -t %3 ; switch-client -t %3",
                    "list-clients -t %3 -F #{client_activity} #{client_pid}",
                ],
            )

    def test_click_focuses_the_nearest_ancestor_window_in_hyprland(self):
        directory = tempfile.TemporaryDirectory(prefix="papo-focus-")
        self.addCleanup(directory.cleanup)
        os.makedirs(os.path.join(directory.name, "hypr", "test_1"))
        clients = [
            {"address": "0xdead", "pid": 1, "title": "other"},
            {"address": "0xbeef", "pid": os.getpid(), "title": "terminal"},
        ]
        socket_path = os.path.join(directory.name, "hypr", "test_1", ".socket.sock")
        self.start_daemon(PAPO_NOTIFY_TEST_CLICK="300")
        with Hyprland(socket_path, clients) as hyprland:
            with R8Mock(count=3) as server:
                t = self.login(
                    server,
                    HYPRLAND_INSTANCE_SIGNATURE="test_1",
                    XDG_RUNTIME_DIR=directory.name,
                )
                self.notify_other_channel(server, t)
                commands = self.wait_commands(t, hyprland, "Hyprland focus")
                t.wait_text("Conteúdo do outro canal")
        self.assertEqual(commands, ["dispatch focuswindow address:0xbeef"])

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
            t.send("\x04\t\n")
            t.wait_text("Preferências")
            t.send("\t\n\x1b\x1b\x1b[O")
            server.emit(self.event(content="conteúdo privado"))
            value = self.receive(t)
            self.assertNotIn("conteúdo privado", value["body"])
            t.send("\x04\t\n\n\x1b\x1b")
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
            t.send("\x04")
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

    def test_channel_mute_and_off_suppress_alerts_and_reactivation_resumes_new_events(self):
        with ChannelPreferencesMock() as server:
            t = self.login(server)
            t.resize(30, 120)
            t.wait_text("Conversar em #geral")

            def menu():
                t.send("\x1b[<2;5;5M\x1b[<2;5;5m")
                t.wait_text("Notificações…")

            def emit(n, content):
                message = dict(server.message(n + 3), content=content)
                server.emit(dict(message, type="message"))
                server.emit(dict(self.event(n, content), message_id=message["id"]))

            def setting(index):
                menu()
                t.send("\x1bOB\n")
                t.wait_text("Todas as mensagens")
                row, line = next(
                    (y, line)
                    for y, line in enumerate(t.screen.display)
                    if "Todas as mensagens" in line
                )
                t.click(row + index, line.index("Todas as mensagens"))
                t.wait_text("Notificações do canal atualizadas")

            menu()
            t.send("\n")
            t.wait_text("10 minutos")
            t.send("\n")
            t.wait_text("Canal silenciado neste cliente")
            t.send("\x1b[O")
            emit(1, "Muted notification")
            t.read(2.5)
            self.assertFalse(select.select([self.daemon.stdout], [], [], 0)[0])
            menu()
            t.wait_text("Reativar canal")
            t.send("\n")
            t.wait_text("Canal reativado")
            emit(2, "Fresh after reactivation")
            self.assertEqual(self.receive(t)["body"], "Fresh after reactivation")
            setting(2)
            emit(3, "Disabled notification")
            t.read(2.5)
            self.assertFalse(select.select([self.daemon.stdout], [], [], 0)[0])
            setting(0)
            emit(4, "Fresh all messages")
            self.assertEqual(self.receive(t)["body"], "Fresh all messages")
            self.assertEqual(server.marked, [])

    def test_device_only_off_suppresses_alerts_without_calling_the_server(self):
        with ChannelPreferencesMock() as server:
            t = self.login(server)
            t.resize(30, 120)
            t.wait_text("Conversar em #geral")

            def notifications():
                t.send("\x1b[<2;5;5M\x1b[<2;5;5m")
                t.wait_text("Notificações…")
                t.send("\x1bOB\n")
                t.wait_text("Sincronizar com o servidor")

            def choose(label):
                row, line = next(
                    (y, line) for y, line in enumerate(t.screen.display) if label in line
                )
                t.click(row, line.index(label))

            def emit(n, content):
                message = dict(server.message(n + 3), content=content)
                server.emit(dict(message, type="message"))
                server.emit(dict(self.event(n, content), message_id=message["id"]))

            def shown(text):
                # Curses rewrites only changed cells, so similar notices never
                # reach the output whole; check the screen instead.
                wait_for_screen(t, lambda: text in "\n".join(t.screen.display), text)

            notifications()
            choose("Sincronizar com o servidor")
            shown("[ ] Sincronizar com o servidor")
            choose("Nada")
            shown("salvas neste dispositivo")
            t.send("\x1b[O")
            emit(1, "Device-only off")
            t.read(2.5)
            self.assertFalse(select.select([self.daemon.stdout], [], [], 0)[0])
            notifications()
            choose("Sincronizar com o servidor")
            shown("sincronizadas com o servidor")
            t.send("m")
            emit(2, "Synced with the server again")
            self.assertEqual(self.receive(t)["body"], "Synced with the server again")
            self.assertEqual(server.setting_calls, [])


if __name__ == "__main__":
    test_main(
        2,
    )
