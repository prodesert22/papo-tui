"""Full R8 UI with synthetic HTTP/WS fixtures."""

import json
from pathlib import Path
import tempfile
import time
import unittest
from r8_mock import R8Mock, DM, AUTHOR, uuid
from test_terminal import Terminal


class R8Terminal(unittest.TestCase):
    def wait(self, t, text):
        end = time.monotonic() + 7
        while text not in "\n".join(t.screen.display) and time.monotonic() < end:
            t.read(0.03)
        self.assertIn(text, "\n".join(t.screen.display))

    def login(self, server, **kwargs):
        t = Terminal(**kwargs)
        self.addCleanup(t.close)
        t.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(t, "[Canais]")
        self.wait(t, "[DMs]")
        self.wait(t, "Categoria")
        return t

    def test_black_background_and_sidebar_focus_include_both_buttons(self):
        with R8Mock(count=0) as server:
            t = self.login(server, environment={"KITTY_WINDOW_ID": "r8-theme-fixture"})
            # pyte leaves untouched cells at "default" after clearing an empty
            # row. Verify the black erase sequence and sample a painted row.
            self.assertIn(b"\x1b[48;5;16m\x1b[H\x1b[2J", t.output)
            resting = t.screen.buffer[7][23].bg
            self.assertEqual(resting, "000000")
            self.assertEqual(t.screen.buffer[10][40].bg, resting)
            t.send("\t")
            self.wait(t, "Foco: Canais/DMs")
            focused = t.screen.buffer[10][23].bg
            self.assertNotEqual(focused, resting)
            t.send("\x1bOC")
            self.assertEqual(t.screen.buffer[10][23].bg, focused)
            t.send("\n\x1bOB")
            self.wait(t, "Foco: DMs")
            self.assertEqual(t.screen.buffer[10][23].bg, focused)
            t.send("\x1bOA")
            self.wait(t, "Foco: Canais/DMs")
            self.assertEqual(t.screen.buffer[10][23].bg, focused)
            t.click(5, 4)
            self.assertEqual(t.screen.buffer[10][23].bg, focused)
            t.send("\x1bOA")
            self.wait(t, "Foco: Abas")
            self.assertEqual(t.screen.buffer[10][23].bg, resting)

    def test_backend_cursor_clears_badge_and_new_activity_relights_it(self):
        with R8Mock(count=3) as server:
            t = self.login(server)

            def badge(present):
                end = time.monotonic() + 4
                while (t.screen.buffer[7][23].data == "●") != present and time.monotonic() < end:
                    t.read(0.03)
                self.assertEqual(t.screen.buffer[7][23].data == "●", present)

            badge(True)
            t.click(7, 4)
            self.wait(t, "Mensagem 0003")
            badge(False)
            # Old notification IDs point outside the current history page.
            # Reading the conversation does not mark unrelated notifications.
            self.assertEqual(server.marked, [])
            self.assertTrue(all(not n["read"] for n in server.notifications))
            t.click(8, 4)
            fresh = server.message(4)
            server.messages.append(fresh)
            server.emit(dict(fresh, type="message"))
            badge(True)
            t.click(7, 4)
            self.wait(t, "Mensagem 0004")
            badge(False)

    def test_history_read_clears_even_if_backend_cursor_lags_but_failed_get_does_not(self):
        from history_mock import CHANNEL

        for case in ("empty", "header-only", "denied", "cursor-write-failed"):
            with self.subTest(case=case), R8Mock(count=3) as server:
                for channel in server.channels:
                    if channel["id"] == CHANNEL:
                        channel["last_message"] = server.summary(server.message(3))
                original = server.api_handler

                def route(method, path, body):
                    if "/channels/" + CHANNEL + "/messages" in path:
                        if case == "denied":
                            return 403, None
                        rows = (
                            []
                            if case == "empty"
                            else [
                                dict(server.message(3), content=None, embeds=None, attachments=[])
                            ]
                        )
                        if case != "cursor-write-failed":
                            channel = next(c for c in server.channels if c["id"] == CHANNEL)
                            channel["last_message"] = server.summary(rows[0]) if rows else None
                            server.touch_read(CHANNEL, rows)
                        return 200, dict(channel_id=CHANNEL, messages=rows, has_more=False)
                    return original(method, path, body)

                server.api_handler = route
                t = self.login(server)
                t.click(7, 4)
                end = time.monotonic() + 5
                while time.monotonic() < end:
                    t.read(0.03)
                    if case == "denied" and "permissão" in "\n".join(t.screen.display):
                        break
                    if case != "denied" and t.screen.buffer[7][23].data != "●":
                        break
                self.assertEqual(t.screen.buffer[7][23].data == "●", case == "denied")
                self.assertEqual(server.marked, [])
                if case == "cursor-write-failed":
                    t.read(0.5)
                    self.assertNotEqual(t.screen.buffer[7][23].data, "●")
                    self.assertEqual(
                        sum("/channels/" + CHANNEL + "/messages" in r[1] for r in server.requests),
                        1,
                    )

    def test_cursor_dots_overlay_buttons_and_long_names_and_ignore_notification_read(self):
        from history_mock import CHANNEL

        with R8Mock() as server:
            for channel in server.channels:
                channel["last_read_message"] = None
                if channel["id"] == CHANNEL:
                    channel["name"] = "Um canal com nome longo demais 界"
            server.dms[0]["unread_count"] = 0
            server.dms[0]["last_message"] = None
            server.dms[0]["user"]["nickname"] = "Destinatário com nome muito longo 界"
            t = self.login(server, environment={"KITTY_WINDOW_ID": "r8-theme-fixture"})

            def dot(row, column, present=True):
                end = time.monotonic() + 4
                while (
                    t.screen.buffer[row][column].data == "●"
                ) != present and time.monotonic() < end:
                    t.read(0.03)
                cell = t.screen.buffer[row][column]
                self.assertEqual(cell.data == "●", present)
                if present:
                    self.assertEqual(cell.fg, "ff0000")
                return cell

            # The backend cursor lights both the channel and its group without
            # fetching any inactive histories.
            dot(5, 10)
            dot(7, 23)
            dot(5, 19, False)
            self.assertFalse(any("/messages" in r[1] for r in server.requests))
            t.click(5, 4)
            group = dot(5, 10)
            self.assertEqual(group.bg, t.screen.buffer[5][3].bg)
            t.send("\x1bOB\x1bOB")
            self.wait(t, "Foco: Canais")
            cell = dot(7, 23)
            self.assertEqual(cell.bg, t.screen.buffer[7][4].bg)
            # While the channel is still unread, add a notification in the DM.
            message = dict(server.message(50), channel_id=DM, author_id=AUTHOR)
            server.emit(dict(message, type="message"))
            server.emit(
                dict(
                    type="new_notification",
                    id=uuid(9000),
                    user_id=AUTHOR,
                    message_id=message["id"],
                    message_content="Nova mensagem",
                )
            )
            dot(5, 19)
            t.click(5, 13)
            dot(6, 23)
            self.assertEqual(t.screen.buffer[5][2].data, "[")
            self.assertEqual(t.screen.buffer[5][12].data, "[")
            self.assertFalse(any("/messages" in r[1] for r in server.requests))
            t.resize(18, 60)
            dot(5, 10)
            dot(5, 19)
            dot(6, 57)
            # Reading a central notification does not advance the conversation
            # cursor. Both conversation badges remain on.
            t.send("\x1b[20~")
            self.wait(t, "Aviso 0001")
            t.send("\n")
            self.wait(t, "Detalhes da notificação")
            t.send("\x1b\x1b")
            dot(5, 10)
            dot(5, 19)
            dot(6, 57)
            self.assertEqual(server.marked, [[uuid(4001)]])
            # GET history persists the read cursor; a metadata refresh confirms
            # it and clears the DM indicators, independently of the channel.
            server.dm_messages = [message]
            t.send("\x1bOB\n")
            self.wait(t, "Mensagem 0050")
            t.read(0.3)
            t.send("\x1bOD")
            self.wait(t, "Foco: DMs")
            dot(5, 19, False)
            dot(6, 57, False)
            dot(5, 10)

    def test_new_process_uses_persisted_channel_and_dm_cursors(self):
        with R8Mock(count=3) as server:
            t = self.login(server)
            t.click(7, 4)
            self.wait(t, "Mensagem 0003")
            t.click(5, 13)
            self.wait(t, "Bia ·")
            t.send("\x1bOB\n")
            self.wait(t, "Mensagem direta")
            t.read(0.5)
            self.assertTrue(all(not n["read"] for n in server.notifications))
            before = sum("/messages" in r[1] for r in server.requests)
            restarted = self.login(server)
            restarted.read(0.5)
            for row, column in ((5, 10), (5, 19), (7, 23)):
                self.assertNotEqual(restarted.screen.buffer[row][column].data, "●")
            restarted.click(5, 13)
            self.wait(restarted, "Bia ·")
            self.assertNotEqual(restarted.screen.buffer[6][23].data, "●")
            self.assertEqual(sum("/messages" in r[1] for r in server.requests), before)

    def test_restart_preserves_reading_when_remote_cursor_stays_old(self):
        with R8Mock(count=3) as server, tempfile.TemporaryDirectory(prefix="r8-reading-") as state:
            server.touch_read = lambda _channel, _messages: None
            environment = {"XDG_STATE_HOME": state}
            t = self.login(server, environment=environment)
            t.click(7, 4)
            self.wait(t, "Mensagem 0003")
            t.click(5, 13)
            self.wait(t, "Bia ·")
            t.send("\x1bOB\n")
            self.wait(t, "Mensagem direta")
            until = time.monotonic() + 5
            while time.monotonic() < until:
                t.read(0.03)
                path = Path(state) / "papo-tui/read-cursors.json"
                if (
                    path.exists()
                    and len(json.loads(path.read_text())["contexts"][0]["messages"]) == 2
                ):
                    break
            self.assertTrue(path.exists())
            before = sum("/messages" in r[1] for r in server.requests)
            restarted = self.login(server, environment=environment)
            restarted.read(0.3)
            self.assertNotEqual(restarted.screen.buffer[5][10].data, "●")
            self.assertNotEqual(restarted.screen.buffer[5][19].data, "●")
            self.assertNotEqual(restarted.screen.buffer[7][23].data, "●")
            restarted.click(5, 13)
            self.wait(restarted, "Bia ·")
            self.assertNotEqual(restarted.screen.buffer[6][23].data, "●")
            self.assertEqual(sum("/messages" in r[1] for r in server.requests), before)

    def test_adjacent_buttons_keyboard_mouse_and_no_history_on_switch(self):
        with R8Mock() as server:
            t = self.login(server)
            t.send("\x1bOB\x1bOB\x1bOD")
            self.wait(t, "Foco: Canais")
            self.assertNotIn("← abas", t.screen.display[-2])
            t.send("\x1bOA")
            self.wait(t, "Foco: Canais")
            t.send("\x1bOD")
            self.wait(t, "Foco: Canais")
            t.send("\x1bOA")
            self.wait(t, "Foco: Canais/DMs")
            t.send("\x1bOA")
            self.wait(t, "Foco: Abas")
            t.send("\t\x1bOC\n")
            self.wait(t, "Bia ·")
            self.assertFalse(any("/messages" in r[1] for r in server.requests))
            t.click(5, 4)
            t.read(0.2)
            self.assertIn("Categoria", "\n".join(t.screen.display))
            t.click(5, 13)
            self.wait(t, "Bia ·")
            self.assertFalse(any("/messages" in r[1] for r in server.requests))
            t.send("\x1bOB\x1bOD")
            self.wait(t, "Foco: DMs")
            t.send("\x1bOA")
            self.wait(t, "Foco: Canais/DMs")
            t.send("\x1bOB\n")
            self.wait(t, "Mensagem direta")
            t.send("rascunho DM\x1b")
            t.click(5, 4)
            self.wait(t, "Categoria")
            self.assertIn("rascunho DM", "\n".join(t.screen.display))
            self.assertIn("Mensagem direta", "\n".join(t.screen.display))

    def test_center_details_marks_only_selected_and_preferences_persist(self):
        with R8Mock(count=3) as server:
            t = self.login(server)
            t.send("\x1b[20~")
            self.wait(t, "Notificações")
            self.wait(t, "Aviso 0003")
            self.assertEqual(server.marked, [])
            t.send("\x1bOB\n")
            self.wait(t, "Detalhes da notificação")
            end = time.monotonic() + 4
            while not server.marked and time.monotonic() < end:
                t.read(0.03)
            self.assertEqual(server.marked, [[uuid(4002)]])
            t.send("\x1b\t\n")
            self.wait(t, "Preferências")
            t.send("\n\t\n")
            path = t.configuration.name + "/papo-tui/config.json"
            with open(path) as f:
                config = json.load(f)
            self.assertEqual(config["version"], 4)
            self.assertFalse(config["desktop_notifications"])
            self.assertFalse(config["notification_preview"])
            t.resize(14, 44)
            self.wait(t, "Preferências")
            t.send("\x1b\x1b")
            self.wait(t, "[Canais]")
            self.wait(t, "[DMs]")

    def test_new_dm_picker_filters_excludes_self_and_banned_and_closes(self):
        with R8Mock() as server:
            t = self.login(server)
            t.send("\x1b[21~")
            self.wait(t, "Nova DM")
            self.wait(t, "Bia (@bob)")
            self.assertNotIn("banido", "\n".join(t.screen.display))
            server.emit(dict(type="presence_update", user_id=uuid(3), status="online"))
            t.read(0.2)
            self.assertNotIn("banido", "\n".join(t.screen.display))
            t.send("nenhum")
            self.wait(t, "Nenhum usuário disponível")
            t.send("\x03")
            self.wait(t, "Bia (@bob)")
            t.send("bia\n")
            self.wait(t, "Mensagem direta")
            t.click(5, 13)
            t.send("\x1bOB\x04")
            self.wait(t, "Nenhuma DM")
            self.assertTrue(
                any(r[0] == "DELETE" and r[1].endswith("/dms/" + DM) for r in server.requests)
            )

    def test_details_survive_ephemeral_promotion_to_different_rest_id(self):
        from r8_mock import notification

        with R8Mock(count=0) as server:
            t = self.login(server)
            server.emit(
                dict(
                    type="new_notification",
                    id=uuid(9000),
                    user_id=AUTHOR,
                    message_id=uuid(5001),
                    message_content="Aviso em promoção",
                )
            )
            t.send("\x1b[20~")
            self.wait(t, "Aviso em promoção")
            t.send("\n")
            self.wait(t, "Detalhes da notificação")
            self.assertEqual(server.marked, [])
            row = notification(1)
            row["message_content"] = "Aviso confirmado"
            server.notifications = [row]
            t.send("\x12")
            self.wait(t, "Aviso confirmado")
            t.read(0.3)
            self.assertEqual(server.marked, [[uuid(4001)]])
            self.assertNotIn("Notificação indisponível", "\n".join(t.screen.display))

    def test_disconnect_notice_only_in_footer_and_priority_at_minimum_width(self):
        with R8Mock() as server:
            t = self.login(server)
            self.wait(t, "Tempo real conectado")
            t.resize(14, 44)
            server.revoked = False
            server.disconnect()
            self.wait(t, "Desconectado")
            self.assertIn("Desconectado", t.screen.display[-2])
            self.assertNotIn("Desconectado", "\n".join(t.screen.display[:-2]))

    def test_inactive_instance_events_and_identical_ids_are_isolated(self):
        with R8Mock() as first, R8Mock() as second:
            first.name = "Primeiro"
            second.name = "Segundo"
            second.session_cookie = "other-instance-fixture"
            first.notifications[0]["message_content"] = "Aviso primeiro servidor"
            second.notifications[0]["message_content"] = "Aviso segundo servidor"
            t = self.login(first)
            t.click(1, 70)
            self.wait(t, "URL do servidor")
            t.send(second.url + "\tjosé\tSynthetic!123\n")
            self.wait(t, "Segundo")
            self.wait(t, "Categoria")
            from r8_mock import notification

            newest = notification(2)
            newest["message_content"] = "Novidade no servidor inativo"
            first.notifications.append(newest)
            first.emit(
                dict(
                    type="new_notification",
                    id=newest["id"],
                    user_id=AUTHOR,
                    message_id=newest["message_id"],
                    message_content=newest["message_content"],
                )
            )
            t.read(0.5)
            t.send("\x1b[20~")
            self.wait(t, "Aviso segundo servidor")
            self.assertNotIn("Aviso primeiro servidor", "\n".join(t.screen.display))
            t.send("\n")
            self.wait(t, "Detalhes da notificação")
            t.read(0.2)
            self.assertEqual(second.marked, [[uuid(4001)]])
            self.assertEqual(first.marked, [])
            t.send("\x1b\x1b")
            t.click(1, 4)
            self.wait(t, "Foco: Abas")
            t.send("\x1b[20~")
            self.wait(t, "Novidade no servidor inativo")
            self.assertNotIn("Aviso segundo servidor", "\n".join(t.screen.display))
            self.assertFalse(any("/messages" in r[1] for r in first.requests))

    def test_presence_and_typing_in_dm_follow_snapshot_and_expire(self):
        with R8Mock(count=0) as server:
            t = self.login(server)
            t.click(5, 13)
            self.wait(t, "Bia ·")
            t.click(6, 4)
            self.wait(t, "Mensagem direta")
            server.emit(dict(type="presence_sync", members=[dict(user_id=AUTHOR, status="online")]))
            self.wait(t, "online")
            server.emit(dict(type="typing", channel_id=DM, user_id=AUTHOR, is_typing=True))
            self.wait(t, "Bia está digitando")
            t.send("atividade real")
            t.read(0.2)
            self.assertTrue(
                any(
                    e.get("type") == "typing" and e.get("channel_id") == DM
                    for e in server.ws_messages
                )
            )
            self.assertLessEqual(sum(e.get("type") == "typing" for e in server.ws_messages), 1)
            server.emit(dict(type="presence_update", user_id=AUTHOR, status="offline"))
            self.wait(t, "offline")
            self.assertNotIn("está digitando", "\n".join(t.screen.display))


if __name__ == "__main__":
    unittest.main()
