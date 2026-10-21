"""Edit/delete interactions through the actual TUI and synthetic backend."""

from test_context import main as test_main

import unittest
from history_mock import CHANNEL, uuid
from interaction_mock import InteractionMock
import terminal_interactions as interactions


class MessageMutations(unittest.TestCase):
    screen = interactions.MessageInteractions.screen
    wait = interactions.MessageInteractions.wait
    editor_text = interactions.MessageInteractions.editor_text
    click = interactions.MessageInteractions.click
    open = interactions.MessageInteractions.open
    right_click = interactions.MessageInteractions.right_click
    settled = interactions.MessageInteractions.settled

    def own(self, server, index=-1):
        server.messages[index]["author_id"] = uuid(1)
        return server.messages[index]

    def edit_from_menu(self, terminal, content):
        self.right_click(terminal, content)
        self.wait(terminal, "Editar mensagem")
        self.click(terminal, "Editar mensagem")
        self.wait(terminal, "Editando mensagem")

    def confirm_delete(self, terminal, content):
        self.right_click(terminal, content)
        self.click(terminal, "Excluir mensagem")
        self.wait(terminal, "Excluir mensagem?")
        terminal.send("\x1bOB\n")

    def test_empty_input_up_edits_latest_own_message_and_enter_uses_put(self):
        with InteractionMock() as server:
            self.own(server, 0)
            target = self.own(server, 1)
            target["embeds"] = []
            server.mutation_broadcast = False  # REST confirmation must suffice.
            terminal = self.open(server)
            try:
                terminal.send("x\x03\x1bOA")
                self.wait(terminal, "Editando mensagem")
                self.assertIn("Mensagem 0002", self.editor_text(terminal))
                terminal.send("\x03Olá, edição!\x0eSegunda linha\n")
                self.settled(terminal, lambda: "Editando mensagem" not in self.screen(terminal))
                self.wait(terminal, "Olá, edição!")
                self.assertEqual(
                    server.mutation_calls,
                    [("PUT", target["id"], {"content": "Olá, edição!\nSegunda linha"})],
                )
                self.assertEqual(server.message_count, 0)
                self.assertEqual(target["embeds"], [])
                self.assertNotIn("Olá", self.editor_text(terminal))
            finally:
                self.assertTrue(terminal.close())

    def test_up_with_text_and_without_own_messages_does_not_start_edit(self):
        with InteractionMock() as server:
            terminal = self.open(server)
            try:
                terminal.send("Draft\x1bOA")
                self.assertNotIn("Editando mensagem", self.screen(terminal))
                self.assertIn("Draft", self.editor_text(terminal))
                terminal.send("\x03\x1bOA")
                self.wait(terminal, "Nenhuma mensagem própria")
                self.assertEqual(server.mutation_calls, [])
                self.assertEqual(server.message_count, 0)
            finally:
                self.assertTrue(terminal.close())

    def test_keyboard_and_mouse_menu_edit_restore_draft_on_escape(self):
        with InteractionMock(count=1) as server:
            self.own(server)
            terminal = self.open(server)
            try:
                terminal.resize(32, 140)
                terminal.send("Preserved draft\x1bOA")
                self.assertNotIn("Editando mensagem", self.screen(terminal))
                terminal.send("\x0bm")
                self.wait(terminal, "Editar mensagem")
                self.wait(terminal, "Excluir mensagem")
                terminal.send("\x1bOB" * 3 + "\n")
                self.wait(terminal, "Editando mensagem")
                self.assertIn("Mensagem 0001", self.editor_text(terminal))
                terminal.send("\x03Discarded edit\x1b")
                self.assertNotIn("Editando mensagem", self.screen(terminal))
                self.assertIn("Preserved draft", self.editor_text(terminal))
                self.wait(terminal, "Foco: Mensagem")
                self.edit_from_menu(terminal, "Mensagem 0001")
                terminal.resize(24, 60)
                self.wait(terminal, "Editando mensagem")
                terminal.send("\x1b")
                self.assertIn("Preserved draft", self.editor_text(terminal))
                self.assertEqual(server.mutation_calls, [])
            finally:
                self.assertTrue(terminal.close())

    def test_edit_option_is_absent_for_other_author_and_unbounded_content_is_refused(self):
        with InteractionMock(count=2) as server:
            target = self.own(server, 0)
            target["content"] = "Long own message " + "x" * 2049
            terminal = self.open(server)
            try:
                self.right_click(terminal, "Mensagem 0002")
                self.wait(terminal, "Excluir mensagem")
                self.assertNotIn("Editar mensagem", self.screen(terminal))
                terminal.send("\x1b")
                # The shortcut considers the newest own message in the loaded
                # window even when it does not fit the editable buffer.
                terminal.send("x\x03\x1bOA")
                self.wait(terminal, "Mensagem excede o limite")
                self.assertNotIn("Editando mensagem", self.screen(terminal))
                self.assertEqual(server.mutation_calls, [])
            finally:
                self.assertTrue(terminal.close())

    def test_edit_errors_keep_buffer_and_never_retry_automatically(self):
        for code in (403, 429, 500):
            with self.subTest(code=code), InteractionMock(count=1) as server:
                self.own(server)
                server.mutation_status = code
                terminal = self.open(server)
                try:
                    self.edit_from_menu(terminal, "Mensagem 0001")
                    terminal.send("\x03Preserved edit\n")
                    self.wait(
                        terminal,
                        "Sem permissão"
                        if code == 403
                        else "Limite de requisições"
                        if code == 429
                        else "Alteração sem confirmação",
                    )
                    self.assertIn("Preserved edit", self.editor_text(terminal))
                    self.assertIn("Editando mensagem", self.screen(terminal))
                    terminal.read(0.4)
                    self.assertEqual(len(server.mutation_calls), 1)
                    if code == 403:
                        server.mutation_status = None
                        terminal.send("\n")
                        self.settled(
                            terminal, lambda: "Editando mensagem" not in self.screen(terminal)
                        )
                        self.assertEqual(len(server.mutation_calls), 2)
                    else:
                        terminal.send("\x1b")
                    self.assertEqual(server.message_count, 0)
                finally:
                    self.assertTrue(terminal.close())

    def test_empty_edit_is_refused_and_draft_reply_survives_cancel(self):
        with InteractionMock(count=1) as server:
            self.own(server)
            terminal = self.open(server)
            try:
                terminal.resize(32, 140)
                terminal.send("Reply draft")
                self.right_click(terminal, "Mensagem 0001")
                self.click(terminal, "Responder")
                self.wait(terminal, "Respondendo a")
                self.edit_from_menu(terminal, "Mensagem 0001")
                terminal.send("\x03")
                self.wait(terminal, "Novo texto da mensagem")
                terminal.send("\n")
                self.wait(terminal, "Escreva uma mensagem antes de editar")
                self.assertEqual(server.mutation_calls, [])
                terminal.send("\x1b")
                self.wait(terminal, "Respondendo a")
                self.assertIn("Reply draft", self.editor_text(terminal))
            finally:
                self.assertTrue(terminal.close())

    def test_typing_during_pending_edit_keeps_edit_mode_and_enter_does_not_duplicate(self):
        with InteractionMock(count=1) as server:
            self.own(server)
            server.mutation_delay = 0.6
            terminal = self.open(server)
            try:
                self.edit_from_menu(terminal, "Mensagem 0001")
                terminal.send("\x03Submitted edit\n\n continued")
                self.wait(terminal, "Mensagem editada")
                self.assertIn("Submitted edit continued", self.editor_text(terminal))
                self.assertIn("Editando mensagem", self.screen(terminal))
                self.assertEqual(len(server.mutation_calls), 1)
                terminal.send("\x1b")
                self.assertEqual(server.message_count, 0)
            finally:
                self.assertTrue(terminal.close())

    def test_uncertain_edit_reconciles_history_without_replaying(self):
        with InteractionMock(count=1) as server:
            target = self.own(server)
            server.mutation_malformed = True
            server.mutation_broadcast = False
            terminal = self.open(server)
            try:
                self.edit_from_menu(terminal, "Mensagem 0001")
                terminal.send("\x03Applied but unconfirmed\n")
                self.wait(terminal, "Alteração sem confirmação")
                self.assertIn("Applied but unconfirmed", self.editor_text(terminal))
                self.settled(
                    terminal, lambda: self.screen(terminal).count("Applied but unconfirmed") >= 2
                )
                terminal.send("\x1b")
                terminal.read(0.3)
                self.assertEqual(len(server.mutation_calls), 1)
                self.assertEqual(target["content"], "Applied but unconfirmed")
            finally:
                self.assertTrue(terminal.close())

    def test_delete_confirmation_escape_default_cancel_and_success(self):
        with InteractionMock(count=1) as server:
            target = self.own(server)
            server.mutation_broadcast = False
            terminal = self.open(server)
            try:
                terminal.send("Preserved draft\x0bm")
                self.wait(terminal, "Excluir mensagem")
                terminal.send("\x1bOB" * 4 + "\n")
                self.wait(terminal, "Excluir mensagem?")
                terminal.send("\n")  # Cancel is selected initially.
                self.assertEqual(server.mutation_calls, [])
                self.right_click(terminal, "Mensagem 0001")
                self.click(terminal, "Excluir mensagem")
                self.wait(terminal, "Excluir mensagem?")
                terminal.send("\x1b")
                self.assertEqual(server.mutation_calls, [])
                self.confirm_delete(terminal, "Mensagem 0001")
                self.settled(terminal, lambda: "Mensagem 0001" not in self.screen(terminal))
                self.assertEqual(server.mutation_calls, [("DELETE", target["id"], None)])
                self.assertIn("Preserved draft", self.editor_text(terminal))
                self.assertEqual(server.message_count, 0)
            finally:
                self.assertTrue(terminal.close())

    def test_mouse_confirms_deletion_and_x_cancels_edit(self):
        with InteractionMock(count=1) as server:
            self.own(server)
            terminal = self.open(server)
            try:
                terminal.send("Preserved draft")
                self.edit_from_menu(terminal, "Mensagem 0001")
                self.click(terminal, "[x]")
                self.assertNotIn("Editando mensagem", self.screen(terminal))
                self.assertIn("Preserved draft", self.editor_text(terminal))
                self.right_click(terminal, "Mensagem 0001")
                self.click(terminal, "Excluir mensagem")
                self.wait(terminal, "Excluir mensagem?")
                for row, line in enumerate(terminal.screen.display):
                    if "Excluir mensagem" in line and "?" not in line:
                        terminal.click(row, line.index("Excluir mensagem") + 2)
                        break
                else:
                    self.fail("Missing delete confirmation")
                self.settled(terminal, lambda: "Mensagem 0001" not in self.screen(terminal))
                self.assertEqual(len(server.mutation_calls), 1)
                self.assertEqual(server.mutation_calls[0][0], "DELETE")
            finally:
                self.assertTrue(terminal.close())

    def test_delete_errors_keep_message_and_do_not_repeat(self):
        for code in (403, 429, 500):
            with self.subTest(code=code), InteractionMock(count=1) as server:
                server.mutation_status = code
                terminal = self.open(server)
                try:
                    self.confirm_delete(terminal, "Mensagem 0001")
                    self.wait(
                        terminal,
                        "Sem permissão"
                        if code == 403
                        else "Limite de requisições"
                        if code == 429
                        else "Alteração sem confirmação",
                    )
                    terminal.read(0.4)
                    self.assertIn("Mensagem 0001", self.screen(terminal))
                    self.assertEqual(len(server.mutation_calls), 1)
                finally:
                    self.assertTrue(terminal.close())

    def test_remote_delete_and_404_cancel_edit_without_sending_a_new_message(self):
        for remote in (False, True):
            with self.subTest(remote=remote), InteractionMock(count=1) as server:
                target = self.own(server)
                terminal = self.open(server)
                try:
                    terminal.send("Preserved draft")
                    self.edit_from_menu(terminal, "Mensagem 0001")
                    if remote:
                        self.settled(terminal, lambda: bool(server.peers))
                        server.messages.clear()
                        server.emit(
                            dict(type="message_delete", id=target["id"], channel_id=CHANNEL)
                        )
                    else:
                        server.mutation_status = 404
                        terminal.send(" changed\n")
                    self.settled(terminal, lambda: "Editando mensagem" not in self.screen(terminal))
                    self.assertIn("Preserved draft", self.editor_text(terminal))
                    self.assertEqual(server.message_count, 0)
                    self.assertEqual(len(server.mutation_calls), 0 if remote else 1)
                finally:
                    self.assertTrue(terminal.close())

    def test_channel_change_during_edit_request_preserves_other_conversation(self):
        with InteractionMock(count=1) as server:
            self.own(server)
            server.mutation_delay = 0.6
            terminal = self.open(server)
            try:
                self.edit_from_menu(terminal, "Mensagem 0001")
                terminal.send("\x03Edited in geral\n")
                self.settled(terminal, lambda: len(server.mutation_calls) == 1)
                terminal.send("\x1b\x1b\x1b\x1bOB\nOther draft")
                self.wait(terminal, "# outro · Histórico")
                terminal.read(0.8)
                self.assertIn("Other draft", self.editor_text(terminal))
                self.assertNotIn("Edited in geral", self.screen(terminal))
                self.assertNotIn("Editando mensagem", self.screen(terminal))
                terminal.send("\x1b\x1b\x1bOA\n")
                self.wait(terminal, "Edited in geral")
                self.assertEqual(len(server.mutation_calls), 1)
            finally:
                self.assertTrue(terminal.close())


if __name__ == "__main__":
    test_main(
        1,
    )
