"""Full R8 UI with synthetic HTTP/WS fixtures."""

import json
from pathlib import Path
import tempfile
import time
import unittest
from r8_mock import R8Mock, DM, AUTHOR, uuid
from test_terminal import Terminal


class R8Terminal(unittest.TestCase):
    def notification_target(self, server, count, index):
        server.count = count
        server.messages = [server.message(i) for i in range(1, count + 1)]
        target = server.messages[index - 1]
        server.notifications[0].update(
            message_id=target["id"],
            channel_id=target["channel_id"],
            message_content="Notificação de destino",
            author_id=target["author_id"],
        )
        return target

    def open_notification_target(self, terminal):
        terminal.send("\x1b[20~")
        self.wait(terminal, "Notificação de destino")
        terminal.send("\n")
        self.wait(terminal, "Detalhes da notificação")
        row = next(
            i for i, line in enumerate(terminal.screen.display) if "[Abrir conversa]" in line
        )
        terminal.click(row, terminal.screen.display[row].index("[Abrir conversa]") + 2)

    def assert_centered_target(self, terminal, content):
        self.wait(terminal, content)
        self.wait(terminal, "↑↓ mensagens")
        terminal.read(0.2)
        row = next(i for i, line in enumerate(terminal.screen.display) if content in line)
        # At 100x32, the history viewport is rows 8..22 and its border is x=27.
        self.assertLessEqual(abs(row - 15), 2, "Target must be centered in history")
        self.assertIn(terminal.screen.buffer[row][27].data, ("x", "│"))
        text_column = terminal.screen.display[row].index(content)
        self.assertNotEqual(
            terminal.screen.buffer[row][27].fg, terminal.screen.buffer[row][text_column].fg
        )

    def test_notification_opens_centers_and_highlights_first_page_and_loaded_target(self):
        for already_open in (False, True):
            with self.subTest(already_open=already_open), R8Mock(count=1) as server:
                target = self.notification_target(server, 60, 30)
                terminal = self.login(server)
                terminal.resize(32, 100)
                if already_open:
                    terminal.click(7, 4)
                    self.wait(terminal, "Mensagem 0060")
                before = sum("/messages" in path for _, path, _, _ in server.requests)
                self.open_notification_target(terminal)
                self.assert_centered_target(terminal, target["content"])
                after = sum("/messages" in path for _, path, _, _ in server.requests)
                self.assertEqual(after - before, 0 if already_open else 1)
                self.assertEqual(server.marked, [[uuid(4001)]])

    def test_notification_opens_old_webhook_target_from_another_channel(self):
        from history_mock import CHANNEL, OTHER

        with R8Mock(count=1) as server:
            target = self.notification_target(server, 320, 40)
            target.update(
                author_id=None, webhook_id=uuid(701), external_author=dict(name="Bot de entregas")
            )
            server.notifications[0]["author_id"] = None
            terminal = self.login(server)
            terminal.resize(32, 100)
            terminal.click(8, 4)
            self.wait(terminal, "Conteúdo do outro canal")
            terminal.send("\x1b[20~")
            self.wait(terminal, "Autor ainda não carregado")
            self.assertFalse(
                any(CHANNEL + "/messages" in path for _, path, _, _ in server.requests)
            )
            terminal.send("\n\n")
            self.assert_centered_target(terminal, target["content"])
            self.wait(terminal, "Bot de entregas")
            reads = [
                path
                for method, path, _, _ in server.requests
                if method == "GET" and "/messages" in path
            ]
            self.assertGreaterEqual(sum(CHANNEL in path for path in reads), 4)
            self.assertEqual(sum(OTHER in path for path in reads), 1)

    def test_notification_resolves_active_webhook_name_and_keeps_it_after_partial_event(self):
        from history_mock import CHANNEL

        with R8Mock(count=1) as server:
            target = self.notification_target(server, 220, 40)
            target.update(
                author_id=None, webhook_id=uuid(702), external_author=dict(name="Bot antigo")
            )
            server.notifications[0]["author_id"] = None
            terminal = self.login(server)
            terminal.resize(32, 100)
            terminal.click(7, 4)
            self.wait(terminal, "Mensagem 0220")
            terminal.send("\x1b[20~")
            self.wait(terminal, "Bot antigo")
            self.assertNotIn("Webhook / autor removido", "\n".join(terminal.screen.display))
            terminal.send("\x1b")
            self.wait(terminal, "Mensagem 0220")
            terminal.click(8, 4)
            self.wait(terminal, "Conteúdo do outro canal")
            before = sum(CHANNEL + "/messages" in path for _, path, _, _ in server.requests)
            partial = dict(target, type="message")
            partial.pop("external_author")
            partial.pop("webhook_id")
            server.emit(partial)
            terminal.send("\x1b[20~")
            self.wait(terminal, "Bot antigo")
            terminal.read(0.3)
            self.assertEqual(
                before, sum(CHANNEL + "/messages" in path for _, path, _, _ in server.requests)
            )

    def test_notification_target_stays_centered_when_content_above_changes(self):
        from history_mock import CHANNEL

        with R8Mock(count=1) as server:
            target = self.notification_target(server, 320, 40)
            terminal = self.login(server)
            terminal.resize(32, 100)
            self.open_notification_target(terminal)
            self.assert_centered_target(terminal, target["content"])
            # A late layout change before an old target must not move the
            # selected message down or out of the viewport.
            server.emit(
                dict(
                    type="message_edit",
                    id=server.messages[38]["id"],
                    channel_id=CHANNEL,
                    content="\n".join(f"Linha tardia {i}" for i in range(12)),
                    edited_at="2026-10-16T12:00:00Z",
                )
            )
            self.wait(terminal, "Linha tardia 11")
            self.assert_centered_target(terminal, target["content"])
            terminal.resize(40, 120)
            self.wait(terminal, target["content"])
            terminal.read(0.2)
            row = next(
                i for i, line in enumerate(terminal.screen.display) if target["content"] in line
            )
            self.assertLessEqual(abs(row - 19), 2)
            # Wheel input releases centering instead of snapping back on the
            # next footer/media redraw. Down/up continue normal message selection.
            terminal.send("\x1b[<64;40;18M")
            terminal.read(0.3)
            moved = next(
                i for i, line in enumerate(terminal.screen.display) if target["content"] in line
            )
            self.assertGreater(moved, row)
            terminal.resize(40, 130)
            terminal.read(0.5)
            self.assertEqual(
                moved,
                next(
                    i for i, line in enumerate(terminal.screen.display) if target["content"] in line
                ),
            )
            terminal.send("\x1bOB\x1bOA")
            self.wait(terminal, target["content"])

    def test_notification_with_null_author_uses_the_loaded_message_author(self):
        with R8Mock(count=1) as server:
            target = self.notification_target(server, 3, 2)
            target["author_id"] = AUTHOR
            server.notifications[0]["author_id"] = None
            terminal = self.login(server)
            terminal.click(7, 4)
            self.wait(terminal, "Mensagem 0003")
            terminal.send("\x1b[20~")
            self.wait(terminal, "Bia")
            self.assertNotIn("Autor ainda não carregado", "\n".join(terminal.screen.display))

    def test_notification_keeps_message_target_while_resolving_unlisted_dm(self):
        from r8_mock import dm

        with R8Mock(count=1) as server:
            server.dms = []
            server.dm_messages = [
                dict(server.message(1000 + i), channel_id=DM, author_id=AUTHOR)
                for i in range(1, 41)
            ]
            target = server.dm_messages[19]
            server.notifications[0].update(
                channel_id=DM, message_id=target["id"], message_content="Notificação de destino"
            )
            original = server.api_handler

            def route(method, path, body):
                if method == "GET" and path.split("?")[0].endswith("/dms/" + DM):
                    return 200, dm()
                return original(method, path, body)

            server.api_handler = route
            terminal = self.login(server)
            terminal.resize(32, 100)
            self.open_notification_target(terminal)
            self.assert_centered_target(terminal, target["content"])
            self.assertTrue(any("/dms/" + DM in request[1] for request in server.requests))
            self.assertEqual(server.opens, 1)

    def test_notification_lookup_failure_requires_retry_and_preserves_target(self):
        with R8Mock(count=1) as server:
            target = self.notification_target(server, 220, 40)
            original = server.api_handler
            unavailable = True

            def route(method, path, body):
                if method == "GET" and "/messages?" in path and "last_id=" in path and unavailable:
                    return 503, None
                return original(method, path, body)

            server.api_handler = route
            terminal = self.login(server)
            terminal.resize(32, 100)
            self.open_notification_target(terminal)
            self.wait(terminal, "Falha ao localizar mensagem")
            before = sum("/messages" in request[1] for request in server.requests)
            terminal.read(0.4)
            self.assertEqual(before, sum("/messages" in request[1] for request in server.requests))
            unavailable = False
            terminal.send("\x12")
            self.assert_centered_target(terminal, target["content"])

    def test_notification_hidden_dm_open_failure_is_not_retried_automatically(self):
        from r8_mock import dm

        for status, notice in ((403, "Acesso recusado"), (429, "Limite de requisições")):
            with self.subTest(status=status), R8Mock(count=1) as server:
                server.dms = []
                server.notifications[0].update(
                    channel_id=DM, message_id=uuid(101), message_content="Notificação de destino"
                )
                original = server.api_handler
                fail_open = True

                def route(method, path, body):
                    if method == "GET" and path.split("?")[0].endswith("/dms/" + DM):
                        return 200, dm()
                    if method == "POST" and path.endswith("/dms") and fail_open:
                        server.opens += 1
                        return status, None
                    return original(method, path, body)

                server.api_handler = route
                terminal = self.login(server)
                terminal.resize(32, 100)
                self.open_notification_target(terminal)
                self.wait(terminal, notice)
                terminal.read(0.4)
                self.assertEqual(server.opens, 1)
                self.assertFalse(any("/messages" in request[1] for request in server.requests))
                fail_open = False
                terminal.send("\x1b[21~")
                self.wait(terminal, "Nova DM")
                terminal.send("bob")
                self.wait(terminal, "Bia (@bob)")
                terminal.send("\n")
                self.wait(terminal, "Mensagem direta")
                terminal.read(0.2)
                self.assertNotIn("↑↓ mensagens", "\n".join(terminal.screen.display))
                self.assertEqual(server.opens, 2)

    def test_notification_reports_missing_target_without_selecting_another_message(self):
        with R8Mock(count=1) as server:
            target = self.notification_target(server, 60, 30)
            server.messages.remove(target)
            terminal = self.login(server)
            terminal.resize(32, 100)
            self.open_notification_target(terminal)
            self.wait(terminal, "Mensagem não disponível")
            self.assertNotIn("↑↓ mensagens", "\n".join(terminal.screen.display))
            before = sum("/messages" in request[1] for request in server.requests)
            terminal.read(0.4)
            self.assertEqual(before, sum("/messages" in request[1] for request in server.requests))

    def test_conversation_selector_search_scroll_empty_and_mouse_selection(self):
        from history_mock import OTHER

        with R8Mock(count=3) as server:
            server.notifications[1]["channel_id"] = OTHER
            server.notifications[2]["channel_id"] = DM
            server.channels += [
                dict(
                    id=uuid(7000 + i),
                    name=f"Canal {i:02d}",
                    type="text",
                    position=10 + i,
                    topic=None,
                )
                for i in range(30)
            ]
            t = self.login(server)
            t.resize(32, 140)
            t.send("\x1b[20~Aviso\t\x1bOC\n")
            self.wait(t, "Selecionar canal")
            t.send("\x1bOB\x1bOC")
            self.wait(t, "Selecionar DM")
            t.send("\x1bOB\x1bOD")
            self.wait(t, "Selecionar canal")
            t.send("\x1bOB\x1bOF")
            self.wait(t, "Canal 29")
            t.send("outro\n")
            self.wait(t, "Filtro: # outro")
            self.assertIn("Buscar: Aviso", "\n".join(t.screen.display))
            self.assertIn("Aviso 0002", "\n".join(t.screen.display))
            self.assertNotIn("Aviso 0001", "\n".join(t.screen.display))
            self.assertNotIn("Aviso 0003", "\n".join(t.screen.display))
            t.click(6, t.screen.display[6].index("[Nome]") + 2)
            self.wait(t, "Selecionar canal")
            t.send("inexistente\n")
            self.wait(t, "Nenhum canal encontrado")
            self.assertIn("Selecionar canal", "\n".join(t.screen.display))
            t.send("\x03geral")
            self.wait(t, "1 canal encontrado")
            t.click(7, 31)
            self.wait(t, "Filtro: # geral")
            self.assertIn("Aviso 0001", "\n".join(t.screen.display))
            self.assertNotIn("Aviso 0002", "\n".join(t.screen.display))
            t.click(6, t.screen.display[6].index("[Nome]") + 2)
            self.wait(t, "Selecionar canal")
            t.click(6, 85)
            self.wait(t, "Selecionar DM")
            t.send("@bob")
            self.wait(t, "Bia (@bob)")
            t.resize(14, 44)
            self.wait(t, "Selecionar DM")
            self.wait(t, "Bia (@bob)")
            t.send("\n")
            self.wait(t, "Filtro: DM: Bia")
            self.wait(t, "Aviso 0003")
            self.assertEqual(server.marked, [])
            self.assertFalse(any("/messages" in r[1] for r in server.requests))

    def test_notification_arrows_reach_filters_without_editing_search(self):
        with R8Mock(count=3) as server:
            server.notifications[1]["channel_id"] = DM
            t = self.login(server)
            t.send("\x1b[20~\x1bOC")
            self.wait(t, "Filtro: Canais")
            self.assertIn("Filtro: Canais [Nome]", "\n".join(t.screen.display))
            self.assertNotIn("Selecionar", "\n".join(t.screen.display))
            self.assertTrue(t.screen.cursor.hidden)
            t.send("\x1bOC")
            self.wait(t, "Filtro: DMs")
            self.assertNotIn("Selecionar", "\n".join(t.screen.display))
            t.send("\x1bOD\n")
            self.wait(t, "Selecionar canal")
            self.assertFalse(t.screen.cursor.hidden)
            t.send("geral\n")
            self.wait(t, "Filtro: # geral")
            self.assertNotIn("Aviso 0002", "\n".join(t.screen.display))
            t.send("\x1b[<64;20;7M\t\n")
            self.wait(t, "Preferências")
            t.send("\x1b\x1bOA\x1bOC\n")
            self.wait(t, "Selecionar DM")
            t.send("@bob\n")
            self.wait(t, "Filtro: DM: Bia")
            self.wait(t, "Aviso 0002")
            self.assertNotIn("Aviso 0003", "\n".join(t.screen.display))
            t.send("\x1bOA\n")
            self.wait(t, "Selecionar DM")
            t.send("\x1b")
            self.wait(t, "Filtro: DM: Bia")
            t.send("\x1bOD")
            self.wait(t, "Filtro: Canais")
            self.assertIn("Filtro: Canais [Nome]", "\n".join(t.screen.display))
            self.assertNotIn("Selecionar", "\n".join(t.screen.display))
            t.send("\x1bOA")
            self.assertFalse(t.screen.cursor.hidden)
            t.send("\x1bOB\t\t\x1bOD")
            self.wait(t, "Data: antigas")
            self.assertTrue(t.screen.cursor.hidden)
            t.send("\x1bOB\x1bOC\n")
            self.wait(t, "Selecionar DM")
            t.send("\t\t\n")
            self.wait(t, "Filtro: DMs")
            t.send("\n")
            self.wait(t, "Detalhes da notificação")
            self.wait(t, "Aviso 0002")
            self.assertEqual(server.marked, [[uuid(4002)]])

    def test_notification_search_mentions_conversation_filters_and_precise_date_order(self):
        with R8Mock(count=3) as server:
            server.notifications[0].update(
                message_content="Mais recente @mention(<@" + uuid(21) + ">)",
                created_at="2026-10-15T12:00:00.123457Z",
            )
            server.notifications[1].update(
                channel_id=DM,
                message_content="Antiga direta",
                created_at="2026-10-15T08:00:00.123456-04:00",
            )
            server.notifications[2].update(
                message_content="Outra no canal", created_at="2026-10-15T12:00:00.123456Z"
            )
            t = self.login(server)
            t.resize(32, 140)
            t.send("\x1b[20~")
            self.wait(t, "@Apelido")
            top, left = 3, 25
            self.assertIn("Filtro: Todos [Nome]", t.screen.display[top + 3])
            self.assertIn("Mais recente", t.screen.display[top + 4])
            self.assertNotIn("@mention(", "\n".join(t.screen.display))
            self.assertEqual(server.marked, [])
            t.click(top + 3, left + 60)
            self.wait(t, "Data: antigas")
            self.assertIn("Antiga direta", t.screen.display[top + 4])
            t.click(top + 3, left + 6)
            self.wait(t, "Filtro: Canais")
            self.assertIn("Filtro: Canais [Nome]", "\n".join(t.screen.display))
            self.assertNotIn("Selecionar", "\n".join(t.screen.display))
            t.click(top + 3, left + 6)
            self.wait(t, "Filtro: DMs")
            self.assertNotIn("Selecionar", "\n".join(t.screen.display))
            t.click(top + 3, left + 6)
            self.wait(t, "Filtro: Todos")
            t.click(top + 3, left + 6)
            self.wait(t, "Filtro: Canais")
            t.click(top + 3, t.screen.display[top + 3].index("[Nome]") + 2)
            self.wait(t, "Selecionar canal")
            t.send("\t\t\n")
            self.wait(t, "Filtro: Canais")
            self.assertNotIn("Antiga direta", "\n".join(t.screen.display))
            t.send("\x1bOC\n")
            self.wait(t, "Selecionar DM")
            t.send("\t\t\n")
            self.wait(t, "Antiga direta")
            self.assertNotIn("Mais recente", "\n".join(t.screen.display))
            self.assertNotIn("Outra no canal", "\n".join(t.screen.display))
            t.send("\x1bOD\n")
            self.wait(t, "Selecionar canal")
            t.send("geral\n")
            self.wait(t, "Filtro: # geral")
            self.wait(t, "Mais recente")
            self.assertNotIn("Antiga direta", "\n".join(t.screen.display))
            t.send("\x1bOA\t\t\x1bOD")
            self.wait(t, "Data: recentes")
            self.assertIn("Mais recente", t.screen.display[top + 4])
            t.send("apelido")
            self.wait(t, "1 resultados")
            self.assertFalse(t.screen.cursor.hidden)
            self.assertEqual(t.screen.cursor.y, top + 2)
            self.assertNotIn("Outra no canal", "\n".join(t.screen.display))
            self.assertEqual(server.marked, [])
            self.assertFalse(any("/messages" in r[1] for r in server.requests))
            t.send("\n\n")
            self.wait(t, "Detalhes da notificação")
            self.wait(t, "@Apelido")
            self.assertNotIn("@mention(", "\n".join(t.screen.display))
            end = time.monotonic() + 4
            while not server.marked and time.monotonic() < end:
                t.read(0.03)
            self.assertEqual(server.marked, [[uuid(4001)]])
            t.send("\x1b")
            t.resize(14, 44)
            self.wait(t, "Buscar:")
            self.wait(t, "@Apelido")
            t.send("\x03inexistente")
            self.wait(t, "Nenhuma notificação")
            t.send("\x03" + "x" * 70 + "\x1bOHinicio")
            self.assertEqual(t.screen.cursor.x, 18)
            self.assertIn("Buscar: inicio", "\n".join(t.screen.display))
            t.send("\x1bOF")
            t.click(3, 14)
            self.assertEqual(t.screen.cursor.x, 14)

    def test_webhook_notification_uses_cached_author_and_red_mention_badges(self):
        with R8Mock(count=0) as server:
            t = self.login(server, environment={"KITTY_WINDOW_ID": "badge-fixture"})
            self.assertIn("F9 avisos", t.screen.display[-2])
            content = "Olá @mention(<@" + uuid(1) + ">)"
            webhook = dict(
                server.message(50),
                author_id=None,
                content=content,
                webhook_id=uuid(700),
                external_author=dict(name="Bot de avisos"),
            )
            server.emit(dict(webhook, type="message"))
            server.emit(
                dict(
                    type="new_notification",
                    id=uuid(9050),
                    user_id=None,
                    message_id=webhook["id"],
                    message_content=content,
                )
            )
            self.wait(t, "❶")
            self.assertIn("[Comunidade 1]", t.screen.display[1])
            self.assertEqual(t.screen.buffer[7][23].data, "❶")
            self.assertEqual(t.screen.buffer[7][23].fg, "ff0000")
            t.send("\x1b[20~")
            self.wait(t, "Bot de avisos")
            self.assertNotIn("Autor indisponível", "\n".join(t.screen.display))
            self.assertFalse(any("/messages" in r[1] for r in server.requests))
            t.send("\n")
            self.wait(t, "Detalhes da notificação")
            self.wait(t, "Bot de avisos")
            t.send("\x1b\x1b")
            self.wait(t, "[Canais]")
            self.assertEqual(t.screen.buffer[7][23].data, "●")
            self.assertIn("[Comunidade 0]", t.screen.display[1])
            self.assertNotIn("❶", t.screen.display[1])
            self.assertEqual(server.marked, [])  # Ephemeral events have no readable row.

    def test_loaded_webhook_author_survives_conversation_change_and_missing_metadata_is_explicit(
        self,
    ):
        from history_mock import OTHER

        with R8Mock(count=2) as server:
            server.messages[0].update(
                author_id=None, webhook_id=uuid(701), external_author=dict(name="Webhook histórico")
            )
            server.notifications[0].update(
                author_id=None,
                message_id=server.messages[0]["id"],
                message_content="Aviso do webhook",
            )
            server.notifications[1].update(author_id=None, message_content="Sem metadados")
            original = server.api_handler

            def route(method, path, body):
                if path.split("?")[0].endswith("/channels/" + OTHER + "/messages"):
                    # Real message UUIDs are unique within an instance. The
                    # generic fixture reuses message(1) in both channels.
                    return 200, dict(
                        channel_id=OTHER,
                        has_more=False,
                        messages=[
                            dict(
                                server.message(99),
                                channel_id=OTHER,
                                content="Conteúdo do outro canal",
                            )
                        ],
                    )
                return original(method, path, body)

            server.api_handler = route
            t = self.login(server)
            t.click(7, 4)
            self.wait(t, "Mensagem 0003")
            t.click(8, 4)
            self.wait(t, "Conteúdo do outro canal")
            t.send("\x1b[20~")
            self.wait(t, "Webhook histórico")
            self.wait(t, "Autor ainda não carregado")
            self.assertNotIn("Autor indisponível", "\n".join(t.screen.display))

    def test_multiple_unread_mentions_are_counted_on_server_and_channel(self):
        with R8Mock(count=12) as server:
            for n in server.notifications:
                n["message_content"] = "Aviso @mention(<@" + uuid(1) + ">)"
            t = self.login(server, environment={"KITTY_WINDOW_ID": "badge-fixture"})
            self.wait(t, "(12)")
            self.assertIn("[Comunidade 12]", t.screen.display[1])
            self.assertIn("(12)", t.screen.display[7])
            self.assertEqual(t.screen.buffer[7][20].fg, "ff0000")
            self.assertFalse(any("/messages" in r[1] for r in server.requests))
            t.resize(32, 140)
            t.send("\x1b[20~\n")
            self.wait(t, "Detalhes da notificação")
            self.wait(t, "(11)")
            self.assertIn("[Comunidade 11]", t.screen.display[1])
            t.send("\x1b\x1b")
            self.assertIn("(11)", t.screen.display[7])

    def test_notification_without_message_metadata_still_lights_server_dot(self):
        with R8Mock(count=0) as server:
            for channel in server.channels:
                channel["last_read_message"] = channel.get("last_message", {}).get("id")
            server.dms[0].update(last_message=None, unread_count=0)
            t = self.login(server)
            self.assertIn("[Comunidade]", t.screen.display[1])
            server.emit(
                dict(
                    type="new_notification",
                    id=uuid(9090),
                    user_id=AUTHOR,
                    message_id=uuid(9091),
                    message_content="Aviso sem metadados",
                )
            )
            self.wait(t, "1 avisos")
            self.assertIn("[Comunidade 0]", t.screen.display[1])
            self.assertFalse(any("/messages" in r[1] for r in server.requests))

    def test_text_server_counter_is_red_and_inside_delimiters(self):
        for mode in ("normal", "light", "ultralight"):
            with self.subTest(mode=mode), R8Mock(count=12) as server:
                root = tempfile.TemporaryDirectory(prefix="papo-badge-mode-")
                self.addCleanup(root.cleanup)
                config = Path(root.name, "papo-tui", "config.json")
                config.parent.mkdir()
                config.write_text(
                    json.dumps(
                        dict(
                            version=4,
                            profiles=[],
                            selected_profile=None,
                            app_mode=mode,
                            requests_per_second=10,
                            request_burst=25,
                            desktop_notifications=False,
                            notification_preview=False,
                        )
                    )
                )
                config.chmod(0o600)
                for n in server.notifications:
                    n["message_content"] = "@mention(<@" + uuid(1) + ">)"
                t = self.login(
                    server,
                    environment={
                        "XDG_CONFIG_HOME": root.name,
                        "KITTY_WINDOW_ID": "badge-fixture",
                        "TERM_PROGRAM": "",
                        "TERM": "xterm-256color",
                    },
                )
                self.wait(t, "[Comunidade 12]")
                line = t.screen.display[1]
                start = line.index("12]")
                self.assertEqual(t.screen.buffer[1][start].fg, "ff0000")
                self.assertEqual(t.screen.buffer[1][start + 1].fg, "ff0000")
                self.assertNotEqual(t.screen.buffer[1][start + 2].fg, "ff0000")
                t.resize(14, 44)
                self.wait(t, "[Comunidade 12]")

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
            # pyte leaves erase-only cells at "default". Sample actual label
            # glyphs after the channel frame is complete, independently of BCE.
            self.wait(t, "geral")
            self.assertIn(b"\x1b[48;5;16m\x1b[H\x1b[2J", t.output)
            deadline = time.monotonic() + 2
            while t.screen.buffer[7][4].bg == "default" and time.monotonic() < deadline:
                t.read(0.03)
            resting = t.screen.buffer[7][4].bg
            self.assertEqual(resting, "000000")
            self.assertEqual(t.screen.buffer[5][30].bg, resting)
            t.send("\t")
            self.wait(t, "Foco: Canais/DMs")
            focused = t.screen.buffer[7][4].bg
            self.assertNotEqual(focused, resting)
            t.send("\x1bOC")
            self.assertEqual(t.screen.buffer[7][4].bg, focused)
            t.send("\n\x1bOB")
            self.wait(t, "Foco: DMs")
            self.assertEqual(t.screen.buffer[7][4].bg, focused)
            t.send("\x1bOA")
            self.wait(t, "Foco: Canais/DMs")
            self.assertEqual(t.screen.buffer[7][4].bg, focused)
            t.click(5, 4)
            self.assertEqual(t.screen.buffer[7][4].bg, focused)
            t.send("\x1bOA")
            self.wait(t, "Foco: Abas")
            self.assertEqual(t.screen.buffer[7][4].bg, resting)

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
            first.notifications[0]["message_content"] = (
                "Aviso primeiro servidor @mention(<@" + uuid(1) + ">)"
            )
            second.notifications[0]["message_content"] = (
                "Aviso segundo servidor @mention(<@" + AUTHOR + ">)"
            )
            t = self.login(first, environment={"KITTY_WINDOW_ID": "badge-fixture"})
            t.click(1, 70)
            self.wait(t, "URL do servidor")
            t.send(second.url + "\tjosé\tSynthetic!123\n")
            self.wait(t, "Segundo")
            self.wait(t, "Categoria")
            self.assertIn("Primeiro 1", t.screen.display[1])
            self.assertIn("Segundo 0", t.screen.display[1])
            from r8_mock import notification

            newest = notification(2)
            newest["message_content"] = "Novidade no servidor inativo @mention(<@" + uuid(1) + ">)"
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
            self.assertIn("Primeiro 2", t.screen.display[1])
            self.assertIn("Segundo 0", t.screen.display[1])
            t.send("\x1b[20~")
            self.wait(t, "Aviso segundo servidor")
            self.assertNotIn("Aviso primeiro servidor", "\n".join(t.screen.display))
            t.send("\n")
            self.wait(t, "Detalhes da notificação")
            t.read(0.2)
            self.assertEqual(second.marked, [[uuid(4001)]])
            self.assertEqual(first.marked, [])
            t.send("\x1b\x1b")
            self.assertIn("Primeiro 2", t.screen.display[1])
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
