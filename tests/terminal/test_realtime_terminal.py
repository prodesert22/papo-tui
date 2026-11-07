"""R7 through the actual conversation screen in a pseudoterminal."""

from test_context import main as test_main

import time
import unittest
from urllib.parse import urlsplit
from realtime_mock import RealtimeMock
from history_mock import CHANNEL, OTHER, HistoryMock, uuid, STAMP, assert_history_reads
import terminal_send as product


EXTRA = [uuid(700 + n) for n in range(9)]


class CacheMock(RealtimeMock):
    """# outro from HistoryMock and nine empty extra channels beside # geral."""

    def __init__(self, count=3, extra=0):
        super().__init__(count=count)
        for n in range(extra):
            self.channels.append(
                {"id": EXTRA[n], "name": f"extra{n}", "position": 10 + n, "type": "text"}
            )

    def route(self, method, path, body):
        resource = urlsplit(path).path
        if "/channels/" in resource and resource.endswith("/messages") and method == "GET":
            channel = resource.split("/")[-2]
            if channel == OTHER:
                return HistoryMock.route(self, method, path, body)
            if channel in EXTRA:
                return 200, {"channel_id": channel, "messages": [], "has_more": False}
        return super().route(method, path, body)


def reads(server, channel):
    return sum(
        1
        for method, path, *_ in server.requests
        if method == "GET" and urlsplit(path).path == f"/papo/channels/{channel}/messages"
    )


class RealtimeTerminal(unittest.TestCase):
    screen = product.SendProduct.screen
    wait = product.SendProduct.wait
    open = product.SendProduct.open
    editor_text = product.SendProduct.editor_text

    def test_channel_navigation_keeps_receiving_current_channel_without_reloading(self):
        with RealtimeMock() as server:
            terminal = self.open(server)
            try:
                terminal.send("Rascunho ao navegar\x1b")
                terminal.click(2, 4)
                terminal.send("\x1bOB\x1bOB")  # buttons -> list -> other channel; keep geral open
                self.assertIn("Foco: Canais", terminal.screen.display[-2])
                self.assertIn("# geral · Histórico", self.screen(terminal))
                message = server.message(4)
                message["content"] = "Recebida enquanto navego nos canais"
                server.emit(dict(message, type="message"))
                self.wait(terminal, message["content"])
                self.assertIn("Rascunho ao navegar", self.editor_text(terminal))
                terminal.send("\x1bOA\n")  # refocus the already open channel
                self.wait(terminal, message["content"])
                # A visible new message can trigger one cursor synchronization.
                assert_history_reads(self, server, {CHANNEL: (1, 2)})
                terminal.send("\x1bOH\x1bOD\x1bOB")
                # Late author summaries legitimately redraw the old messages;
                # settle that update before asserting the new event won't scroll.
                self.wait(terminal, "Apelido")
                before = [row[26:] for row in terminal.screen.display[7:11]]
                message = server.message(5)
                message["content"] = "Recebida sem deslocar a leitura"
                server.emit(dict(message, type="message"))
                self.wait(terminal, "1 novas mensagens")
                self.assertEqual(before, [row[26:] for row in terminal.screen.display[7:11]])
                terminal.send("\x1bOA\x1bOC\x1bOF")
                self.wait(terminal, message["content"])
                self.assertIn("Rascunho ao navegar", self.editor_text(terminal))
                assert_history_reads(self, server, {CHANNEL: (1, 3)})
            finally:
                self.assertTrue(terminal.close())

    def test_embed_only_message_renders_live_without_reopening_channel(self):
        with RealtimeMock(count=0) as server:
            terminal = self.open(server)
            try:
                terminal.send("Rascunho durante embed")
                # Keep the full expanded card visible above the padded editor.
                terminal.resize(30, 80)
                message = dict(server.message(1), content="", embeds=None)
                server.emit(
                    {k: v for k, v in dict(message, type="message").items() if k != "embeds"}
                )
                terminal.read(0.2)
                event = {
                    "type": "message_embeds_update",
                    "channel_id": CHANNEL,
                    "message_id": message["id"],
                    "embeds": [
                        {
                            "id": uuid(901),
                            "source_type": "custom",
                            "fetch_method": "manual",
                            "title": "Título recebido via WS",
                            "description": "Descrição ao vivo",
                            "fields": [{"name": "Campo remoto", "value": "Valor do embed"}],
                        }
                    ],
                }
                server.emit(event)
                self.wait(terminal, "Título recebido via WS")
                self.wait(terminal, "Descrição ao vivo")
                self.wait(terminal, "Valor do embed")
                self.assertIn("Rascunho durante embed", self.editor_text(terminal))
                assert_history_reads(self, server, {CHANNEL: (1, 2)})
                server.emit(dict(event, embeds=[{"title": "Novo título"}]))
                self.wait(terminal, "Novo título")
                self.assertNotIn("Título recebido via WS", self.screen(terminal))
                server.emit(dict(event, embeds=[]))
                terminal.read(0.3)
                self.assertNotIn("Novo título", self.screen(terminal))
                server.emit(event)
                self.wait(terminal, "Título recebido via WS")
                server.emit(dict(event, embeds=None))
                terminal.read(0.3)
                self.assertNotIn("Título recebido via WS", self.screen(terminal))
            finally:
                self.assertTrue(terminal.close())

    def test_receive_and_duplicate_without_manual_read(self):
        with RealtimeMock() as server:
            terminal = self.open(server)
            try:
                m = server.message(4)
                m["content"] = "Recebida automaticamente á"
                server.emit(dict(m, type="message"), fragmented=True)
                self.wait(terminal, m["content"])
                server.emit(dict(m, type="message"))
                terminal.read(0.2)
                self.assertEqual(self.screen(terminal).count(m["content"]), 1)
                assert_history_reads(self, server, {CHANNEL: (1, 2)})
            finally:
                self.assertTrue(terminal.close())

    def test_scrolled_history_indicates_new_messages_and_deleted_anchor(self):
        with RealtimeMock(count=50) as server:
            terminal = self.open(server)
            try:
                terminal.send("\x1bOH")
                self.wait(terminal, "Mensagem 0001")
                self.wait(terminal, "Apelido")
                before = terminal.screen.display[7:11]
                m = server.message(51)
                m["content"] = "Novidade fora da leitura"
                server.emit(dict(m, type="message"))
                self.wait(terminal, "1 novas mensagens")
                self.assertEqual(before, terminal.screen.display[7:11])
                server.emit({"type": "message_delete", "channel_id": CHANNEL, "id": uuid(101)})
                terminal.read(0.3)
                self.assertIn("Mensagem 0002", self.screen(terminal))
                terminal.send("\x1bOF")
                self.wait(terminal, m["content"])
                self.assertNotIn("novas mensagens", self.screen(terminal))
            finally:
                self.assertTrue(terminal.close())

    def test_background_events_leave_the_add_server_caret_alone(self):
        with RealtimeMock() as server:
            terminal = self.open(server)
            try:
                self.wait(terminal, "Tempo real conectado")
                terminal.click(0, terminal.screen.display[0].index("[+ Adicionar]") + 2)
                self.wait(terminal, "URL do servidor")
                terminal.read(0.5)
                self.assertFalse(terminal.screen.cursor.hidden)
                start = len(terminal.output)
                # The connected server keeps receiving events behind the form.
                for n in range(8):
                    status = "online" if n % 2 else "idle"
                    server.emit(dict(type="presence_update", user_id=uuid(21), status=status))
                    server.emit(dict(type="typing_start", channel_id=CHANNEL, user_id=uuid(21)))
                    terminal.read(0.1)
                terminal.read(0.3)
                # Its tab label is unchanged, so the form is not repainted and
                # the caret is not hidden and shown again, which stops blinking.
                self.assertNotIn(b"\x1b[?25h", bytes(terminal.output[start:]))
                self.assertFalse(terminal.screen.cursor.hidden)
            finally:
                self.assertTrue(terminal.close())

    def switch_away_and_back(self, terminal):
        terminal.click(5, 4)
        self.wait(terminal, "Conteúdo do outro canal")
        mark = len(terminal.output)
        terminal.click(4, 4)
        self.wait(terminal, "# geral · Histórico")
        return mark

    def test_switching_back_reopens_the_cached_channel_without_reading(self):
        with CacheMock() as server:
            terminal = self.open(server)
            try:
                self.wait(terminal, "Mensagem 0003")
                before = reads(server, CHANNEL)
                mark = self.switch_away_and_back(terminal)
                self.wait(terminal, "Mensagem 0003")
                terminal.read(0.4)
                self.assertNotIn(b"Carregando hist", terminal.output[mark:])
                self.assertEqual(reads(server, CHANNEL), before)
                # Returning to # outro also uses its cached window.
                terminal.click(5, 4)
                self.wait(terminal, "Conteúdo do outro canal")
                terminal.read(0.3)
                self.assertEqual(reads(server, OTHER), 1)
            finally:
                self.assertTrue(terminal.close())

    def test_cached_channel_reopens_where_the_reader_left_it(self):
        with CacheMock(count=50) as server:
            terminal = self.open(server)
            try:
                terminal.send("\x1bOH")
                self.wait(terminal, "Mensagem 0001")
                before = reads(server, CHANNEL)
                self.switch_away_and_back(terminal)
                self.wait(terminal, "Mensagem 0001")
                self.assertNotIn("Mensagem 0050", self.screen(terminal))
                self.assertEqual(reads(server, CHANNEL), before)
            finally:
                self.assertTrue(terminal.close())

    def test_message_while_away_loads_on_return_at_the_end(self):
        with CacheMock() as server:
            terminal = self.open(server)
            try:
                self.wait(terminal, "Mensagem 0003")
                terminal.click(5, 4)
                self.wait(terminal, "Conteúdo do outro canal")
                before = reads(server, CHANNEL)
                message = dict(server.message(4), content="Chegou enquanto fora")
                with server.lock:
                    server.messages.append(message)
                server.emit(dict(message, type="message"))
                terminal.read(0.4)
                terminal.click(4, 4)
                self.wait(terminal, "Chegou enquanto fora")
                self.assertIn("Mensagem 0003", self.screen(terminal))
                # Only the latest page, which also marks the conversation read.
                self.assertGreaterEqual(reads(server, CHANNEL), before + 1)
                self.assertLessEqual(reads(server, CHANNEL), before + 2)
            finally:
                self.assertTrue(terminal.close())

    def test_message_while_away_waits_for_scrolling_down_when_reading_older(self):
        with CacheMock(count=50) as server:
            terminal = self.open(server)
            try:
                terminal.send("\x1bOH")
                self.wait(terminal, "Mensagem 0001")
                terminal.click(5, 4)
                self.wait(terminal, "Conteúdo do outro canal")
                before = reads(server, CHANNEL)
                message = dict(server.message(51), content="Nova lá embaixo")
                with server.lock:
                    server.messages.append(message)
                server.emit(dict(message, type="message"))
                terminal.read(0.4)
                terminal.click(4, 4)
                self.wait(terminal, "Mensagem 0001")
                terminal.read(0.4)
                # Nothing is read in advance while the reader is above it.
                self.assertEqual(reads(server, CHANNEL), before)
                terminal.send("\x1bOF")
                self.wait(terminal, "Nova lá embaixo")
                self.assertGreater(reads(server, CHANNEL), before)
            finally:
                self.assertTrue(terminal.close())

    def test_edit_while_away_reloads_the_cached_channel(self):
        with CacheMock() as server:
            terminal = self.open(server)
            try:
                self.wait(terminal, "Mensagem 0002")
                terminal.click(5, 4)
                self.wait(terminal, "Conteúdo do outro canal")
                before = reads(server, CHANNEL)
                with server.lock:
                    server.messages[1]["content"] = "Editada enquanto fora"
                    server.messages[1]["edited_at"] = STAMP
                server.emit(
                    {
                        "type": "message_edit",
                        "id": server.messages[1]["id"],
                        "channel_id": CHANNEL,
                        "content": "Editada enquanto fora",
                        "edited_at": STAMP,
                    }
                )
                terminal.read(0.4)
                terminal.click(4, 4)
                self.wait(terminal, "Editada enquanto fora")
                self.assertGreater(reads(server, CHANNEL), before)
            finally:
                self.assertTrue(terminal.close())

    def test_reconnect_and_explicit_retry_drop_the_cached_channel(self):
        with CacheMock() as server:
            terminal = self.open(server)
            try:
                self.wait(terminal, "Mensagem 0003")
                self.switch_away_and_back(terminal)  # # outro is now cached.
                self.wait(terminal, "Mensagem 0003")
                server.disconnect()
                self.wait(terminal, "Desconectado")
                deadline = time.monotonic() + 8
                while "Desconectado" in self.screen(terminal) and time.monotonic() < deadline:
                    terminal.read(0.05)
                self.assertNotIn("Desconectado", self.screen(terminal))
                before = reads(server, OTHER)
                terminal.click(5, 4)
                self.wait(terminal, "Conteúdo do outro canal")
                terminal.read(0.3)
                # Events may have been lost while disconnected.
                self.assertEqual(reads(server, OTHER), before + 1)
                terminal.click(4, 4)
                self.wait(terminal, "Mensagem 0003")
                before = reads(server, CHANNEL)
                terminal.send("\x12")  # Ctrl+R always reads again.
                deadline = time.monotonic() + 5
                while reads(server, CHANNEL) == before and time.monotonic() < deadline:
                    terminal.read(0.05)
                self.assertGreater(reads(server, CHANNEL), before)
            finally:
                self.assertTrue(terminal.close())

    def test_only_the_eight_most_recent_channels_stay_cached(self):
        with CacheMock(extra=8) as server:
            terminal = self.open(server, environment=None)
            try:
                terminal.resize(40, 100)
                self.wait(terminal, "extra7")
                self.wait(terminal, "Mensagem 0003")
                before = reads(server, CHANNEL)
                for n in range(8):
                    row = next(
                        i for i, line in enumerate(terminal.screen.display) if f"extra{n}" in line
                    )
                    terminal.click(row, terminal.screen.display[row].index(f"extra{n}"))
                    self.wait(terminal, f"# extra{n} · Histórico")
                    self.wait(terminal, "Nenhuma mensagem neste canal.")
                terminal.click(4, 4)
                self.wait(terminal, "Mensagem 0003")
                terminal.read(0.3)
                # # geral was the least recently used of nine: it is read again.
                self.assertGreater(reads(server, CHANNEL), before)
                self.assertTrue(all(reads(server, EXTRA[n]) == 1 for n in range(8)))
            finally:
                self.assertTrue(terminal.close())

    def test_reconnect_keeps_draft_resize_and_offline_changes(self):
        with RealtimeMock() as server:
            terminal = self.open(server)
            try:
                terminal.send("Rascunho preservado")
                server.offline = True
                route = server.api_handler

                def unavailable(method, path, body):
                    if server.offline and method == "GET" and path.endswith("/ws"):
                        return 503, None
                    return route(method, path, body)

                server.api_handler = unavailable
                server.disconnect()
                self.wait(terminal, "Desconectado")
                terminal.send(" durante a queda")
                terminal.resize(22, 60)
                self.assertIn("Rascunho preservado", self.editor_text(terminal))
                with server.lock:
                    server.messages[-1]["content"] = "Edição offline recuperada"
                    server.messages[-1]["edited_at"] = STAMP
                    server.messages.pop(0)
                server.offline = False
                self.wait(terminal, "Edição offline recuperada")
                self.assertIn("durante a queda", self.editor_text(terminal))
                self.assertFalse(
                    any(r[0] == "POST" and r[1].endswith("/messages") for r in server.requests)
                )
            finally:
                self.assertTrue(terminal.close())

    def test_concurrent_ws_and_post_confirmation_preserve_edit(self):
        with RealtimeMock(count=0) as server:

            def created(message):
                server.emit(dict(message, type="message"))
                server.emit(
                    {
                        "type": "message_edit",
                        "id": message["id"],
                        "channel_id": CHANNEL,
                        "content": "Editada antes da confirmação",
                        "edited_at": STAMP,
                    }
                )
                time.sleep(0.4)

            server.message_hook = created
            terminal = self.open(server)
            try:
                terminal.send("Texto enviado\ncontinuação")
                self.wait(terminal, "Mensagem confirmada")
                self.wait(terminal, "Editada antes da confirmação")
                self.assertIn("continuação", self.editor_text(terminal))
                self.assertNotIn("Texto enviado", self.editor_text(terminal))
                self.assertNotIn("Texto enviado", self.screen(terminal))
                self.assertEqual(server.message_count, 1)
            finally:
                self.assertTrue(terminal.close())

    def test_remote_controls_and_channel_deletion_do_not_submit_draft(self):
        with RealtimeMock() as server:
            terminal = self.open(server)
            try:
                terminal.send("Rascunho do canal removido")
                m = server.message(4)
                m["content"] = "Texto\u001b]52;c;REMOTE\u0007fim"
                server.emit(dict(m, type="message"))
                terminal.read(0.3)
                self.assertNotIn(b"\x1b]52;c;REMOTE", terminal.output)
                server.emit({"type": "channel_delete", "channel_id": CHANNEL})
                self.wait(terminal, "Canal removido")
                terminal.send("\n")
                terminal.read(0.2)
                self.assertEqual(server.message_count, 0)
            finally:
                self.assertTrue(terminal.close())

    def test_initial_identity_failure_does_not_create_recovery_session(self):
        with RealtimeMock() as server:
            route = server.route
            server.api_handler = lambda method, path, body: (
                (503, None) if path.endswith("/auth/whoami") else route(method, path, body)
            )
            terminal = product.Terminal()
            try:
                terminal.send(server.url + "\tjosé\tSynthetic!123\n")
                self.wait(terminal, "Serviço indisponível")
                self.assertIn("[ Entrar ]", self.screen(terminal))
                self.assertFalse(any(r[1].endswith("/ws") for r in server.requests))
            finally:
                self.assertTrue(terminal.close())


if __name__ == "__main__":
    test_main(
        1,
    )
