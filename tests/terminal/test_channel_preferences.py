"""Channel menus, remote settings and scoped local mutes through synthetic PTYs."""

from test_context import main as test_main, protocols
import hashlib
import json
from pathlib import Path
import tempfile
import threading
import time
from channel_preferences import ChannelPreferencesMock
from history_mock import CHANNEL, OTHER, uuid
from terminal_history import HistoryTerminal
from terminal_support import Terminal, wait_for_screen
from terminal_media import MediaTerminal


class ChannelPreferences(HistoryTerminal):
    def open(self, server, environment=None):
        t = Terminal(environment=environment)
        t.resize(30, 120)
        self.addCleanup(lambda: t.close() if t.process.poll() is None else None)
        t.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(t, "Conversar em #geral")
        return t

    def menu(self, t, name="outro", *, keyboard=False):
        def target():
            return next(
                (
                    (y, line)
                    for y, line in enumerate(t.screen.display)
                    if y >= 6 and f"# {name}" in line[:25]
                ),
                None,
            )

        wait_for_screen(t, lambda: target() is not None, "Channel row was not rendered")
        row, line = target()
        column = line.index("#")
        if keyboard:
            t.click(row, column)
            # An empty sidebar row focuses the list without toggling the editor.
            t.click(12, column)
            self.wait(t, "Foco: Canais")
            t.send("M")
        else:
            t.send(f"\x1b[<2;{column + 1};{row + 1}M\x1b[<2;{column + 1};{row + 1}m")
        self.wait(t, "Notificações…")

    def settings(self, t, index):
        t.send("\x1bOB\n")
        self.wait(t, "Todas as mensagens")
        # A freshly loaded channel defaults to mentions, so select by clicking.
        row, line = next(
            (y, line) for y, line in enumerate(t.screen.display) if "Todas as mensagens" in line
        )
        t.click(row + index, line.index("Todas as mensagens"))

    def test_menu_changes_inactive_channel_without_reading_its_history(self):
        with ChannelPreferencesMock() as server:
            t = self.open(server)
            try:
                self.menu(t)
                self.settings(t, 0)
                self.wait(t, "Notificações do canal atualizadas")
                self.assertEqual(server.setting_calls, [(OTHER, "all")])

                def reconciled():
                    last_post = max(
                        i
                        for i, r in enumerate(server.requests)
                        if r[0] == "POST" and r[1].endswith("/settings")
                    )
                    return any(
                        r[0] == "GET" and r[1].endswith("/channels")
                        for r in server.requests[last_post + 1 :]
                    )

                wait_for_screen(t, reconciled, "Confirmed preference did not refresh metadata")
                self.assertFalse(any(OTHER + "/messages" in r[1] for r in server.requests))
                self.menu(t)
                t.send("\x1bOB\n")
                self.wait(t, "[x] Todas as mensagens")
                t.send("\x1b")
                self.wait(t, "Notificações…")
                t.send("\x1b")
                self.wait(t, "Conversar em #geral")
                for index, mode in ((2, "off"), (1, "only_mentions")):
                    self.menu(t)
                    self.settings(t, index)
                    self.wait(t, "Notificações do canal atualizadas")
                    self.assertEqual(server.setting_calls[-1], (OTHER, mode))
            finally:
                self.assertTrue(t.close())

    def test_all_mute_durations_restart_and_explicit_reactivation(self):
        with tempfile.TemporaryDirectory() as root, ChannelPreferencesMock() as server:
            t = self.open(server, environment={"XDG_CONFIG_HOME": root})
            try:
                for index, seconds in enumerate((600, 1800, 3600, 14400, 43200, 86400, -1)):
                    self.menu(t, "geral", keyboard=True)
                    t.send("\n")
                    self.wait(t, "Até eu reativar")
                    t.send("\x1bOB" * index + "\n")
                    self.wait(t, "Canal silenciado neste cliente")
                    files = list(Path(root, "papo-tui/channel-mutes").glob("*.json"))
                    self.assertEqual(len(files), 1)
                    expires = json.loads(files[0].read_text())["channels"][CHANNEL]
                    if seconds < 0:
                        self.assertEqual(expires, -1)
                    else:
                        self.assertLessEqual(abs(expires - int(time.time()) - seconds), 2)
                    self.menu(t, "geral")
                    self.wait(t, "Reativar canal")
                    t.send("\n")
                    self.wait(t, "Canal reativado")
                self.menu(t, "geral")
                t.send("\n" + "\x1bOB" * 6 + "\n")
                self.wait(t, "Canal silenciado neste cliente")
            finally:
                self.assertTrue(t.close())
            second = self.open(server, environment={"XDG_CONFIG_HOME": root})
            try:
                self.menu(second, "geral")
                self.wait(second, "Reativar canal")
                second.send("\n")
                self.wait(second, "Canal reativado")
            finally:
                self.assertTrue(second.close())
            self.assertEqual(server.setting_calls, [])

    def test_failed_settings_do_not_replay_or_show_a_false_selection(self):
        for code in (403, 429, 500):
            with self.subTest(code=code), ChannelPreferencesMock() as server:
                server.setting_status = code
                t = self.open(server)
                try:
                    self.menu(t)
                    self.settings(t, 0)
                    self.wait(
                        t,
                        "Sem permissão"
                        if code == 403
                        else "Limite de requisições"
                        if code == 429
                        else "Resultado não confirmado",
                    )
                    t.read(0.3)
                    self.assertEqual(len(server.setting_calls), 1)
                    self.menu(t)
                    t.send("\x1bOB\n")
                    self.wait(t, "[x] Apenas menções")
                finally:
                    self.assertTrue(t.close())

    def test_saved_timed_mute_expires_while_the_client_is_idle(self):
        with tempfile.TemporaryDirectory() as root, ChannelPreferencesMock() as server:
            digest = hashlib.sha256(f"{server.url}\n{uuid(1)}".encode()).hexdigest()
            path = Path(root, "papo-tui/channel-mutes", digest + ".json")
            path.parent.mkdir(parents=True)
            path.write_text(json.dumps(dict(version=1, channels={CHANNEL: int(time.time()) + 8})))
            path.chmod(0o600)
            t = self.open(server, environment={"XDG_CONFIG_HOME": root})
            try:
                self.wait(t, "(silenciado)")
                wait_for_screen(
                    t,
                    lambda: "(silenciado)" not in "\n".join(t.screen.display),
                    "Timed mute did not expire without input",
                    seconds=12,
                )
                self.menu(t, "geral")
                self.wait(t, "Silenciar canal…")
                self.assertEqual(server.setting_calls, [])
                self.assertEqual(server.marked, [])
            finally:
                self.assertTrue(t.close())

    def test_cancel_preserves_draft_and_permission_revocation_closes_channel_menu(self):
        with ChannelPreferencesMock() as server:
            role = uuid(6000)
            server.owner_id = uuid(99)
            server.roles = [dict(id=role, name="Channel reader", permissions={})]
            server.user_roles = [dict(id=role)]
            target = next(c for c in server.channels if c["id"] == OTHER)
            target["permissions"] = [dict(role_id=role, permissions=dict(read_channel=True))]
            t = self.open(server)
            try:
                t.click(t.screen.lines - 7, 31)
                self.wait(t, "Foco: Mensagem")
                t.send("Preserved channel menu draft")
                self.wait(t, "Preserved channel menu draft")
                self.menu(t)
                t.send("\n")
                self.wait(t, "Até eu reativar")
                t.send("m")
                self.wait(t, "Preserved channel menu draft")
                self.assertNotIn("Até eu reativar", "\n".join(t.screen.display))
                self.menu(t)
                server.user_roles = []
                server.emit(dict(type="role_remove", user_id=uuid(1), role_id=role))
                wait_for_screen(
                    t,
                    lambda: (
                        "Notificações…" not in "\n".join(t.screen.display)
                        and not any("# outro" in line[:25] for line in t.screen.display)
                    ),
                    "Revoked channel remained available in the context menu",
                )
                self.assertEqual(server.setting_calls, [])
                self.assertFalse(any(OTHER + "/messages" in r[1] for r in server.requests))
                self.wait(t, "Preserved channel menu draft")
            finally:
                self.assertTrue(t.close())

    def test_pending_setting_survives_channel_selection_and_invalid_receipt_reconciles(self):
        with ChannelPreferencesMock() as server:
            server.setting_hold = threading.Event()
            t = self.open(server)
            try:
                self.menu(t)
                self.settings(t, 0)
                wait_for_screen(t, server.setting_started.is_set, "Setting was not transmitted")
                t.click(8, 4)
                server.setting_hold.set()
                self.wait(t, "Notificações do canal atualizadas")
                self.wait(t, "Conteúdo do outro canal")
                self.assertEqual(server.setting_calls, [(OTHER, "all")])
                server.setting_hold = None
                server.setting_invalid = True
                self.menu(t)
                self.settings(t, 2)
                self.wait(t, "Resultado não confirmado")
                t.read(0.3)
                self.assertEqual(server.setting_calls, [(OTHER, "all"), (OTHER, "off")])
            finally:
                server.setting_hold.set() if server.setting_hold else None
                self.assertTrue(t.close())

    def test_old_notifications_do_not_keep_server_badge_after_a_successful_read(self):
        with ChannelPreferencesMock() as server:
            server.notifications = [
                dict(
                    id=uuid(4001),
                    message_id=server.messages[-1]["id"],
                    channel_id=CHANNEL,
                    author_id=uuid(2),
                    message_content="Old notification",
                    read=False,
                    created_at=server.messages[-1]["created_at"],
                )
            ]
            t = self.open(server)
            try:
                self.wait(t, "Mensagem 0003")
                wait_for_screen(
                    t,
                    lambda: "[Comunidade]" in t.screen.display[1],
                    "Read channel kept a server activity badge",
                )
                self.assertEqual(server.marked, [])
                self.assertFalse(server.notifications[0]["read"])
                t.click(8, 4)
                fresh = server.message(4)
                server.messages.append(fresh)
                server.emit(dict(fresh, type="message"))
                wait_for_screen(
                    t,
                    lambda: "[Comunidade 0]" in t.screen.display[1],
                    "Fresh message did not light server badge",
                )
                t.click(7, 4)
                self.wait(t, "Mensagem 0004")
                wait_for_screen(
                    t,
                    lambda: "[Comunidade]" in t.screen.display[1],
                    "Read cursor did not clear server badge",
                )
                server.emit(dict(server.message(2), type="message"))
                t.read(0.3)
                self.assertIn("[Comunidade]", t.screen.display[1])
            finally:
                self.assertTrue(t.close())


class ChannelMenuMedia(MediaTerminal):
    def fallback_cells(self, t):
        return {
            (y, x): (
                c.data,
                c.fg if c.data.strip() else "",
                c.bg if c.data != "█" else "",
            )
            for y in range(8, t.screen.lines - 7)
            for x in range(62, t.screen.columns - 2)
            for c in (t.screen.buffer[y][x],)
        }

    def wait_fallback(self, t):
        def image_cells():
            def colorful(value):
                if len(value) != 6:
                    return False
                channels = [int(value[i : i + 2], 16) for i in (0, 2, 4)]
                return max(channels) - min(channels) >= 64

            return {
                p: c
                for p, c in self.fallback_cells(t).items()
                if any(colorful(value) for value in c[1:])
            }

        wait_for_screen(
            t,
            lambda: len({c[1:] for c in image_cells().values()}) > 8,
            "Chafa image cells were not rendered outside the menu",
        )
        # Compare visible image colors; blank padding and invisible foregrounds
        # may change when ncurses repaints the surrounding windows.
        return image_cells()

    def assert_fallback(self, t, expected):
        actual = self.fallback_cells(t)
        changed = [(p, c, actual.get(p)) for p, c in expected.items() if actual.get(p) != c]
        self.assertFalse(changed[:5], "Uncovered Chafa image cells changed")

    def background_parts(self, t, protocol, frame_start, placement_start, original):
        if protocol == "kitty":
            dimensions = {
                int(h[b"i"]): (int(h[b"s"]), int(h[b"v"])) for h, _ in t.stream.kitty_frames
            }
            current = {}
            for p, position in zip(t.stream.placements, t.stream.placement_positions):
                current[(int(p[b"i"]), int(p[b"p"]))] = (p, position)
            images = [
                (dimensions.get(key[0], (0, 0)), position)
                for key, (p, position) in current.items()
                if key in t.stream.live_placements
            ]
        else:
            images = [
                (self.raster_size(t, i), t.stream.frame_positions[i])
                for i in range(frame_start, len(t.stream.frames))
            ]
        return [
            (size, point)
            for size, point in images
            if max(size) > 64 and (size[0] < original[0] or size[1] < original[1])
        ]

    def assert_uncovered(self, t, protocol, label, label_row, checkpoint, original):
        row, line = next((y, line) for y, line in enumerate(t.screen.display) if label in line)
        left = line.index(label) - (5 if label_row == 3 else 1)
        top, right, bottom = (
            row - label_row,
            left + 34,
            row - label_row + (9 if label_row == 7 else 5 if label_row == 3 else 4),
        )

        def parts():
            return self.background_parts(t, protocol, *checkpoint, original)

        wait_for_screen(t, lambda: bool(parts()), "Uncovered image fragments disappeared")
        for (w, h), (y, x) in parts():
            self.assertTrue(
                y + (h + 15) // 16 <= top or y >= bottom or x + (w + 7) // 8 <= left or x >= right,
                (protocol, label, (y, x, w, h), (top, left, bottom, right)),
            )

    def test_media_remains_visible_around_channel_menu_and_submenus(self):
        for protocol in protocols("kitty", "sixel", "iterm2", "chafa", "light", "ultralight"):
            with (
                self.subTest(protocol=protocol),
                self.serve(self.large_images["landscape"], "image/png") as server,
            ):
                environment = {"KITTY_WINDOW_ID": "channel-fixture"}
                with tempfile.TemporaryDirectory() as root:
                    if protocol in ("light", "ultralight"):
                        config = Path(root, "papo-tui/config.json")
                        config.parent.mkdir()
                        config.write_text(
                            json.dumps(
                                dict(
                                    version=4,
                                    profiles=[],
                                    selected_profile=None,
                                    requests_per_second=10,
                                    request_burst=25,
                                    desktop_notifications=True,
                                    notification_preview=True,
                                    app_mode=protocol,
                                )
                            )
                        )
                        config.chmod(0o600)
                        environment["XDG_CONFIG_HOME"] = root
                    graphical = protocol in ("kitty", "sixel", "iterm2")
                    t = self.open(
                        server,
                        sixel=graphical,
                        protocol=protocol if graphical else None,
                        size=(34, 120),
                        environment=environment,
                    )
                    try:
                        if graphical:
                            self.wait_frames(t)
                            before = len(t.stream.frames)
                            original = self.raster_size(t)
                            checkpoint = (before, len(t.stream.placements))
                        elif protocol in ("chafa", "light"):
                            fallback = self.wait_fallback(t)
                        t.send("\x1b[<2;5;8M\x1b[<2;5;8m")
                        self.wait(t, "Silenciar canal…")
                        if protocol in ("chafa", "light"):
                            self.assert_fallback(t, fallback)
                        if graphical:
                            self.assert_uncovered(
                                t, protocol, "Notificações…", 2, checkpoint, original
                            )
                            checkpoint = (len(t.stream.frames), len(t.stream.placements))
                        t.send("\n")
                        self.wait(t, "Até eu reativar")
                        if protocol in ("chafa", "light"):
                            self.assert_fallback(t, fallback)
                        if graphical:
                            self.assert_uncovered(
                                t, protocol, "Até eu reativar", 7, checkpoint, original
                            )
                            checkpoint = (len(t.stream.frames), len(t.stream.placements))
                        t.resize(38, 130)
                        self.wait(t, "Até eu reativar")
                        if protocol in ("chafa", "light"):
                            fallback = self.wait_fallback(t)
                        if graphical:
                            self.assert_uncovered(
                                t, protocol, "Até eu reativar", 7, checkpoint, original
                            )
                            checkpoint = (len(t.stream.frames), len(t.stream.placements))
                        t.send("\x1b\x1bOB\n")
                        self.wait(t, "Apenas menções")
                        if protocol in ("chafa", "light"):
                            self.assert_fallback(t, fallback)
                        if graphical:
                            self.assert_uncovered(t, protocol, "Nada", 3, checkpoint, original)
                        t.send("\x1b\x1b")
                        self.wait(t, "Conversar em #geral")
                        if protocol in ("chafa", "light"):
                            self.assert_fallback(t, fallback)
                        if graphical:
                            self.assertGreaterEqual(len(t.stream.frames), before)
                            self.assertTrue(t.stream.frames)
                        elif protocol == "ultralight":
                            self.assertFalse(t.stream.frames)
                            self.assertFalse(any("/attachments/" in r[1] for r in server.requests))
                    finally:
                        self.assertTrue(t.close())


if __name__ == "__main__":
    test_main(2)
