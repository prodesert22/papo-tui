"""R7 through the actual conversation screen in a pseudoterminal."""

import time
import unittest
from realtime_mock import RealtimeMock
from history_mock import CHANNEL, uuid, STAMP, assert_history_reads
import test_send_product as product


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
                terminal.click(5, 4)
                terminal.send("\x1bOB\x1bOB")  # buttons -> list -> other channel; keep geral open
                self.assertIn("Foco: Canais", terminal.screen.display[2])
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
                before = [row[26:] for row in terminal.screen.display[8:12]]
                message = server.message(5)
                message["content"] = "Recebida sem deslocar a leitura"
                server.emit(dict(message, type="message"))
                self.wait(terminal, "1 novas mensagens")
                self.assertEqual(before, [row[26:] for row in terminal.screen.display[8:12]])
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
                before = terminal.screen.display[8:12]
                m = server.message(51)
                m["content"] = "Novidade fora da leitura"
                server.emit(dict(m, type="message"))
                self.wait(terminal, "1 novas mensagens")
                self.assertEqual(before, terminal.screen.display[8:12])
                server.emit({"type": "message_delete", "channel_id": CHANNEL, "id": uuid(101)})
                terminal.read(0.3)
                self.assertIn("Mensagem 0002", self.screen(terminal))
                terminal.send("\x1bOF")
                self.wait(terminal, m["content"])
                self.assertNotIn("novas mensagens", self.screen(terminal))
            finally:
                self.assertTrue(terminal.close())

    def test_reconnect_keeps_draft_resize_and_offline_changes(self):
        with RealtimeMock() as server:
            terminal = self.open(server)
            try:
                terminal.send("Rascunho preservado")
                server.offline = True
                server.disconnect()
                self.wait(terminal, "Reconectando")
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
    unittest.main()
