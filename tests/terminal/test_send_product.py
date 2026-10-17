"""R6 through the actual login/conversation screen with synthetic HTTP data."""

import os
import time
import unittest
from history_mock import HistoryMock, CHANNEL
from test_terminal import Terminal, editor_region, editor_column
from wcwidth import wcswidth


class SendProduct(unittest.TestCase):
    def screen(self, terminal):
        return "\n".join(terminal.screen.display)

    def wait(self, terminal, text):
        deadline = time.monotonic() + 8
        while text not in self.screen(terminal) and time.monotonic() < deadline:
            terminal.read(0.03)
        self.assertIn(text, self.screen(terminal))

    def click(self, terminal, text):
        self.wait(terminal, text)
        for row, line in enumerate(terminal.screen.display):
            if text in line:
                terminal.click(row, wcswidth(line[: line.index(text)]) + 1)
                return
        self.fail("Missing control: " + text)

    def open(self, server, *, environment=None):
        terminal = Terminal(environment=environment)
        if os.name == "nt":
            # Register before login assertions, which may fail before returning
            # the terminal to a caller's try/finally block.
            self.addCleanup(terminal.close)
        terminal.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(terminal, "geral")
        terminal.send("\x1bOB\n")
        self.wait(terminal, "# geral · Histórico")
        self.wait(terminal, "Conversar em #geral")
        return terminal

    def editor_text(self, terminal):
        # Editor input is separated from message history and status lines.
        rows = terminal.screen.display
        first, end = editor_region(terminal)
        return "\n".join(rows[first:end])

    def test_confirmed_message_merges_without_get_and_keeps_later_edits(self):
        with HistoryMock(count=0) as server:
            server.scenario = "send-delayed"
            terminal = self.open(server)
            try:
                before = len([r for r in server.requests if r[0] == "GET" and "/messages" in r[1]])
                terminal.send("Olá, ação!\x0esegunda linha\n")
                terminal.send(" continuação")
                self.wait(terminal, "Mensagem confirmada")
                self.assertEqual(server.sent_messages[0]["content"], "Olá, ação!\nsegunda linha")
                self.assertEqual(server.sent_messages[0]["channel_id"], CHANNEL)
                self.assertEqual(server.message_count, 1)
                self.assertIn("continuação", self.editor_text(terminal))
                self.assertNotIn("Olá", self.editor_text(terminal))
                self.assertIn("Olá, ação!", self.screen(terminal))
                self.assertIn("josé", self.screen(terminal))
                self.assertEqual(
                    before,
                    len([r for r in server.requests if r[0] == "GET" and "/messages" in r[1]]),
                )
                terminal.send("\x1b\x12")
                self.wait(terminal, "segunda linha")
                self.assertEqual(self.screen(terminal).count("Olá, ação!"), 1)
                terminal.resize(14, 44)
                self.assertIn("continuação", self.editor_text(terminal))
                self.assertNotIn(b"Synthetic!123", terminal.output)
            finally:
                self.assertTrue(terminal.close())

    def test_draft_cursor_mouse_and_return_from_account(self):
        with HistoryMock(count=0) as server:
            terminal = self.open(server)
            try:
                terminal.send("Primeiro\x1bOD\x1bOD\x1b\x1b\x1bOB\n")
                self.wait(terminal, "# outro · Histórico")
                terminal.send("Segundo\x1b\x1b\x1bOA\n")
                self.wait(terminal, "# geral · Histórico")
                self.assertIn("Primeiro", self.editor_text(terminal))
                terminal.send("X")
                self.assertIn("PrimeiXro", self.editor_text(terminal))
                terminal.send("\x1b[17~")
                self.wait(terminal, "[Canais / F6]")
                terminal.send("\x1b[17~")
                self.wait(terminal, "geral")
                terminal.send("\x1bOB\n")
                self.wait(terminal, "# geral · Histórico")
                self.assertIn("PrimeiXro", self.editor_text(terminal))
                terminal.click(editor_region(terminal)[0], editor_column(terminal) + 3)
                self.wait(terminal, "Foco: Mensagem")
                terminal.send("Y")
                self.assertIn("Y", self.editor_text(terminal))
                self.assertEqual(server.message_count, 0)
            finally:
                self.assertTrue(terminal.close())

    def test_channel_switch_during_post_does_not_clear_or_merge_other_channel(self):
        with HistoryMock(count=0) as server:
            server.scenario = "send-delayed"
            terminal = self.open(server)
            try:
                terminal.send("Texto do geral\n\x1b\x1b\x1bOB\nRascunho outro")
                self.wait(terminal, "# outro · Histórico")
                terminal.read(0.5)
                self.assertNotIn("Texto do geral", self.screen(terminal))
                self.assertIn("Rascunho outro", self.editor_text(terminal))
                self.assertEqual(server.sent_messages[0]["channel_id"], CHANNEL)
                terminal.send("\x1b\x1b\x1bOA\n")
                self.wait(terminal, "Texto do geral")
                self.assertNotIn("Texto do geral", self.editor_text(terminal))
                self.assertEqual(server.message_count, 1)
            finally:
                self.assertTrue(terminal.close())

    def test_permission_error_retains_draft_and_manual_retry(self):
        with HistoryMock(count=0) as server:
            server.scenario = "send-forbidden"
            terminal = self.open(server)
            try:
                terminal.send("Texto preservado\n")
                self.wait(terminal, "Sem permissão")
                self.assertIn("Texto preservado", self.editor_text(terminal))
                server.scenario = "public"
                terminal.send("\x14")
                self.wait(terminal, "Mensagem confirmada")
                self.assertEqual(server.message_count, 2)
                self.assertNotIn("Texto preservado", self.editor_text(terminal))
            finally:
                self.assertTrue(terminal.close())

    def test_rewriting_while_post_runs_preserves_new_draft(self):
        with HistoryMock(count=0) as server:
            server.scenario = "send-delayed"
            terminal = self.open(server)
            try:
                terminal.send("Texto capturado\n\x15Novo rascunho")
                self.wait(terminal, "Mensagem confirmada")
                self.assertEqual(server.sent_messages[0]["content"], "Texto capturado")
                self.assertIn("Novo rascunho", self.editor_text(terminal))
                self.assertNotIn("Texto capturado", self.editor_text(terminal))
            finally:
                self.assertTrue(terminal.close())

    def test_send_failures_keep_draft_and_401_refresh_never_replays(self):
        cases = [
            ("send-validation", "Mensagem recusada"),
            ("send-missing", "Conversa indisponível"),
            ("send-limited", "Limite de envios"),
            ("send-server", "Envio sem confirmação"),
            ("send-auth", "Envio não autorizado"),
        ]
        for scenario, notice in cases:
            with self.subTest(scenario=scenario), HistoryMock(count=0) as server:
                server.scenario = scenario
                terminal = self.open(server)
                try:
                    terminal.send("Texto após falha\n")
                    self.wait(terminal, notice)
                    terminal.read(0.4)
                    self.assertEqual(server.message_count, 1)
                    self.assertIn("Texto após falha", self.editor_text(terminal))
                    if scenario == "send-auth":
                        self.assertEqual(server.refresh_count, 1)
                finally:
                    self.assertTrue(terminal.close())

    def test_uncertain_default_cancel_and_explicit_keyboard_retry(self):
        with HistoryMock(count=0) as server:
            server.scenario = "send-drop-once"
            terminal = self.open(server)
            try:
                terminal.send("Mensagem incerta\n")
                self.wait(terminal, "Envio sem confirmação")
                terminal.send("\n")
                self.wait(terminal, "Reenviar mensagem?")
                terminal.send("\n")  # Default is Cancelar, never another POST.
                self.assertEqual(server.message_count, 1)
                terminal.send("\x14")
                self.wait(terminal, "Reenviar mensagem?")
                terminal.send("\x1b")
                self.assertEqual(server.message_count, 1)
                terminal.send("\x14\x1bOC\n")
                self.wait(terminal, "Mensagem confirmada")
                self.assertEqual(server.message_count, 2)
                self.assertNotIn("Mensagem incerta", self.editor_text(terminal))
            finally:
                self.assertTrue(terminal.close())

    def test_paste_blank_text_and_narrow_arrow_navigation(self):
        with HistoryMock(count=0) as server:
            terminal = self.open(server)
            try:
                terminal.send("   \n")
                self.assertEqual(server.message_count, 0)
                terminal.send("\x15\x1b[200~Ação\r\nsegunda linha\x11\x1b[201~")
                self.assertEqual(server.message_count, 0)
                terminal.resize(22, 44)
                self.assertIn("segunda linha", self.editor_text(terminal))
                terminal.send("\x1b\x1bOD")
                self.wait(terminal, "Foco: Canais")
                terminal.send("\x1bOD")
                self.assertIn("Foco: Canais", self.screen(terminal))
                terminal.send("\x1bOH\x1bOA\x1bOA")
                self.wait(terminal, "Foco: Abas")
                terminal.send("\x1bOB\x1bOC")
                self.wait(terminal, "# geral · Histórico")
                terminal.send("\n\n")
                terminal.resize(24, 100)
                self.wait(terminal, "Mensagem confirmada")
                self.assertEqual(server.sent_messages[0]["content"], "Ação\nsegunda linha")
            finally:
                self.assertTrue(terminal.close())

    def test_same_channel_uuid_in_two_servers_preserves_independent_drafts(self):
        with HistoryMock(count=0) as first, HistoryMock(count=0) as second:
            second.name = "Trabalho"
            terminal = self.open(first)
            try:
                terminal.send("Rascunho primeiro")
                self.click(terminal, "[+ Adicionar]")
                self.wait(terminal, "URL do servidor")
                terminal.send(second.url + "\tjosé\tSynthetic!123\n")
                self.wait(terminal, "[Trabalho")
                self.wait(terminal, "geral")
                terminal.send("\x1bOB\nRascunho segundo")
                self.assertIn("Rascunho segundo", self.editor_text(terminal))
                self.click(terminal, "Comunidade")
                self.wait(terminal, "[Comunidade")
                self.wait(terminal, "geral")
                terminal.send("\x1bOB\n")
                self.wait(terminal, "# geral · Histórico")
                self.assertIn("Rascunho primeiro", self.editor_text(terminal))
                self.assertNotIn("Rascunho segundo", self.editor_text(terminal))
                terminal.send("\n\n")
                self.wait(terminal, "Mensagem confirmada")
                self.assertEqual(first.message_count, 1)
                self.assertEqual(second.message_count, 0)
                self.click(terminal, "Trabalho")
                self.wait(terminal, "[Trabalho")
                self.wait(terminal, "geral")
                terminal.send("\x1bOB\n")
                self.wait(terminal, "# geral · Histórico")
                self.assertIn("Rascunho segundo", self.editor_text(terminal))
            finally:
                self.assertTrue(terminal.close())


if __name__ == "__main__":
    unittest.main()
