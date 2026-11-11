"""R6 through the actual login/conversation screen with synthetic HTTP data."""

from test_context import main as test_main

import json
import os
from pathlib import Path
import stat
import tempfile
from history_mock import HistoryMock, CHANNEL
from terminal_support import CTRL_J, Terminal, editor_region, editor_column, wait_for_screen


import terminal_send as support


class SendProduct(support.SendProduct):
    def history_text(self, terminal):
        first, _ = editor_region(terminal)
        return "\n".join(terminal.screen.display[:first])

    def test_optimistic_message_shows_sending_until_confirmed(self):
        with HistoryMock(count=0) as server:
            server.scenario = "send-held"
            terminal = self.open(server)
            try:
                terminal.send("Mensagem otimista\n")
                self.wait(terminal, "enviando")
                # The POST is still held: the text already left the editor.
                self.assertIn("Mensagem otimista", self.history_text(terminal))
                self.assertNotIn("Mensagem otimista", self.editor_text(terminal))
                self.assertIn("josé", self.history_text(terminal))
                # No server identity yet: a right click opens no message actions.
                row = next(
                    y
                    for y, line in enumerate(terminal.screen.display)
                    if "Mensagem otimista" in line
                )
                column = terminal.screen.display[row].index("Mensagem otimista")
                terminal.send(f"\x1b[<2;{column + 1};{row + 1}M\x1b[<2;{column + 1};{row + 1}m")
                terminal.read(0.3)
                self.assertNotIn("Responder", self.screen(terminal))
                server.send_release.set()
                self.wait(terminal, "Mensagem confirmada")
                self.assertNotIn("enviando", self.screen(terminal))
                self.assertEqual(self.history_text(terminal).count("Mensagem otimista"), 1)
                self.assertEqual(server.message_count, 1)
            finally:
                server.send_release.set()
                self.assertTrue(terminal.close())

    def test_enter_sends_only_from_the_message_box_and_ctrl_j_selects_messages(self):
        with HistoryMock(count=3) as server:
            terminal = self.open(server)
            try:
                self.wait(terminal, "Mensagem 0003")
                terminal.send("Rascunho guardado")
                self.wait(terminal, "Foco: Mensagem")
                # Ctrl+J enters and leaves message selection; it never sends.
                terminal.send(CTRL_J)
                self.wait(terminal, "Foco: Seleção")
                terminal.send(CTRL_J)
                self.wait(terminal, "Foco: Mensagem")
                # Enter outside the message box only moves the focus.
                terminal.send("\x1b[1;3A")
                self.wait(terminal, "Foco: Histórico")
                terminal.send("\n")
                self.wait(terminal, "Foco: Mensagem")
                terminal.send("\x1b[1;3D")
                self.wait(terminal, "Foco: Canais")
                terminal.send("\n")
                terminal.read(0.5)
                self.assertEqual(server.sent_messages, [])
                self.assertIn("Rascunho guardado", self.editor_text(terminal))
                # Ctrl+A selects the whole text, so typing replaces it.
                terminal.send("\x1b[1;3C")
                self.wait(terminal, "Foco: Histórico")
                terminal.send("\x1b[1;3B")
                self.wait(terminal, "Foco: Mensagem")
                terminal.send("\x01Texto novo")
                wait_for_screen(
                    terminal,
                    lambda: "Rascunho" not in self.editor_text(terminal),
                    "Ctrl+A did not select the whole draft",
                )
                self.assertIn("Texto novo", self.editor_text(terminal))
                self.assertEqual(server.sent_messages, [])
                # Shift+Enter breaks the line; Enter in the box sends.
                terminal.send("\x1b[13;2usegunda linha\n")
                wait_for_screen(
                    terminal, lambda: len(server.sent_messages) == 1, "Enter did not send"
                )
                self.assertEqual(server.sent_messages[0]["content"], "Texto novo\nsegunda linha")
            finally:
                self.assertTrue(terminal.close())

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
                terminal.send("\x14")
                self.wait(terminal, "[Canais]")
                terminal.send("\x14")
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

    def test_permission_error_keeps_failed_message_and_manual_retry(self):
        with HistoryMock(count=0) as server:
            server.scenario = "send-forbidden"
            terminal = self.open(server)
            try:
                terminal.send("Texto preservado\n")
                self.wait(terminal, "Sem permissão")
                self.wait(terminal, "falhou")
                self.assertIn("Texto preservado", self.history_text(terminal))
                self.assertNotIn("Texto preservado", self.editor_text(terminal))
                server.scenario = "public"
                terminal.send("\x19")
                self.wait(terminal, "Mensagem confirmada")
                self.assertEqual(server.message_count, 2)
                # The retry replaces the failed entry instead of showing it twice.
                self.assertNotIn("falhou", self.screen(terminal))
                self.assertEqual(self.history_text(terminal).count("Texto preservado"), 1)
            finally:
                self.assertTrue(terminal.close())

    def test_rewriting_while_post_runs_preserves_new_draft(self):
        with HistoryMock(count=0) as server:
            server.scenario = "send-delayed"
            terminal = self.open(server)
            try:
                terminal.send("Texto capturado\n\x03Novo rascunho")
                self.wait(terminal, "Mensagem confirmada")
                self.assertEqual(server.sent_messages[0]["content"], "Texto capturado")
                self.assertIn("Novo rascunho", self.editor_text(terminal))
                self.assertNotIn("Texto capturado", self.editor_text(terminal))
            finally:
                self.assertTrue(terminal.close())

    def test_send_failures_keep_failed_message_and_401_refresh_never_replays(self):
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
                    tag = "não confirmado" if scenario == "send-server" else "falhou"
                    self.wait(terminal, tag)
                    self.assertIn("Texto após falha", self.history_text(terminal))
                    self.assertNotIn("Texto após falha", self.editor_text(terminal))
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
                self.wait(terminal, "não confirmado")
                terminal.send("\x19")
                self.wait(terminal, "Reenviar mensagem?")
                terminal.send("\n")  # Default is Cancelar, never another POST.
                self.assertEqual(server.message_count, 1)
                terminal.send("\x19")
                self.wait(terminal, "Reenviar mensagem?")
                terminal.send("\x1b")
                self.assertEqual(server.message_count, 1)
                terminal.send("\x19\x1bOC\n")
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
                terminal.send("\x03\x1b[200~Ação\r\nsegunda linha\x11\x1b[201~")
                self.assertEqual(server.message_count, 0)
                terminal.resize(22, 44)
                self.assertIn("segunda linha", self.editor_text(terminal))
                terminal.send("\x1b\x1bOD")
                self.wait(terminal, "Foco: Canais")
                terminal.send("\x1bOD")
                self.assertIn("Foco: Canais", self.screen(terminal))
                terminal.send("\x1bOH\x1bOA\x1bOA")
                self.wait(terminal, "Foco: Servidores")
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

    def reopen(self, server, environment):
        """Signs in again and opens # geral, where a restored draft replaces the placeholder."""
        terminal = Terminal(environment=environment)
        self.addCleanup(lambda: terminal.close() if terminal.process.poll() is None else None)
        terminal.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(terminal, "geral")
        terminal.send("\x1bOB\n")
        self.wait(terminal, "# geral · Histórico")
        self.wait(terminal, "Mensagem 0250")
        return terminal

    def test_drafts_return_after_restarting_and_leave_with_the_account(self):
        with HistoryMock() as server, tempfile.TemporaryDirectory(prefix="papo-drafts-") as state:
            environment = {"XDG_STATE_HOME": state}
            saved = Path(state, "papo-tui", "drafts.json")
            t = self.open(server, environment=environment)
            t.send("Rascunho guardado")
            wait_for_screen(t, lambda: "Rascunho guardado" in self.editor_text(t), "draft typed")
            # The draft is written after a pause in typing, privately.
            wait_for_screen(
                t,
                lambda: saved.is_file() and "Rascunho guardado" in saved.read_text("utf-8"),
                "draft not saved",
            )
            self.assertEqual(stat.S_IMODE(os.stat(saved).st_mode), 0o600)
            self.assertNotIn("Synthetic!123", saved.read_text("utf-8"))
            self.assertTrue(t.close())
            self.assertEqual(server.sent_messages, [])

            t = self.reopen(server, environment)
            wait_for_screen(
                t, lambda: "Rascunho guardado" in self.editor_text(t), "draft not restored"
            )
            self.assertEqual(server.sent_messages, [])
            # Signing out removes the account's drafts from this computer.
            t.send("\x0c")
            self.wait(t, "URL do servidor")
            wait_for_screen(
                t,
                lambda: "Rascunho guardado" not in saved.read_text("utf-8"),
                "draft kept after signing out",
            )
            self.assertEqual(json.loads(saved.read_text("utf-8"))["contexts"], [])
            self.assertTrue(t.close())

            t = self.reopen(server, environment)
            self.wait(t, "Conversar em #geral")
            self.assertNotIn("Rascunho guardado", self.editor_text(t))
            self.assertTrue(t.close())


if __name__ == "__main__":
    test_main(1, verbosity=2)
