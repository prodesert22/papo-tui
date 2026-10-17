"""Message selection, replies and reactions through the actual TUI.

Reaction fixtures use distinct BMP glyphs, which fit ncurses' Windows wchar_t.
The native input suites separately verify non-BMP input and message payloads.
"""

import json
from pathlib import Path
import re
import time
import unittest
from wcwidth import wcswidth
from history_mock import CHANNEL, OTHER, uuid
from interaction_mock import InteractionMock
import test_send_product as product


class MessageInteractions(unittest.TestCase):
    screen = product.SendProduct.screen
    wait = product.SendProduct.wait
    editor_text = product.SendProduct.editor_text
    click = product.SendProduct.click

    def open(self, server):
        # Keep Unicode labels observable for text interaction assertions. The
        # graphics suites cover rendered icons; mode tests exercise all modes.
        # Let the app create its private configuration with native permissions.
        terminal = product.SendProduct.open(self, server)
        try:
            terminal.send("\x1bOQ")
            self.wait(terminal, "Configurações")
            self.wait(terminal, "(*) Normal")
            terminal.send("\t\x1bOB\n\t\n")
            path = Path(terminal.configuration.name, "papo-tui", "config.json")
            self.settled(
                terminal,
                lambda: path.exists() and json.loads(path.read_text()).get("app_mode") == "light",
            )
            self.wait(terminal, "Conversar em #geral")
            return terminal
        except BaseException:
            terminal.close()
            raise

    def right_click(self, terminal, text):
        self.wait(terminal, text)
        for row, line in enumerate(terminal.screen.display):
            if text in line:
                column = wcswidth(line[: line.index(text)]) + 1
                terminal.send(f"\x1b[<2;{column + 1};{row + 1}M\x1b[<2;{column + 1};{row + 1}m")
                return
        self.fail("Missing message: " + text)

    def settled(self, terminal, predicate, seconds=6):
        deadline = time.monotonic() + seconds
        while not predicate() and time.monotonic() < deadline:
            terminal.read(0.03)
        self.assertTrue(predicate(), self.screen(terminal))

    def test_f7_selects_messages_and_context_menu_is_available_directly(self):
        with InteractionMock() as server:
            terminal = self.open(server)
            try:
                terminal.resize(32, 100)
                self.wait(terminal, "Mensagem 0003")
                terminal.send("Draft\x1b[18~")
                self.wait(terminal, "↑↓ mensagens")
                self.assertNotIn("Escolha uma mídia", self.screen(terminal))
                self.assertNotIn("Nenhuma mídia", self.screen(terminal))
                terminal.send("\n")
                self.assertNotIn("Ver quem reagiu", self.screen(terminal))
                self.assertIn("↑↓ mensagens", self.screen(terminal))
                self.assertEqual(server.message_count, 0)
                terminal.send("\x1bOAm")
                self.wait(terminal, "Ver quem reagiu")
                self.wait(terminal, "Adicionar reação")
                terminal.send("m")
                self.assertNotIn("Ver quem reagiu", self.screen(terminal))
                self.assertIn("↑↓ mensagens", self.screen(terminal))
                terminal.send("m")
                self.wait(terminal, "Ver quem reagiu")
                terminal.send("\x1b\x1b")
                self.right_click(terminal, "Mensagem 0001")
                self.wait(terminal, "Responder")
                self.click(terminal, "Responder")
                self.wait(terminal, "Respondendo a")
                self.assertIn("Draft", self.editor_text(terminal))
                terminal.send("\n")
                self.wait(terminal, "Mensagem confirmada")
                request = next(
                    r for r in server.requests if r[0] == "POST" and r[1].endswith("/messages")
                )
                self.assertEqual(request[3]["reply_to"], server.messages[0]["id"])
                self.assertEqual(server.sent_messages[0]["reply_to"], server.messages[0]["id"])
                self.assertNotIn("Respondendo a", self.screen(terminal))
                self.assertNotIn(b"Synthetic!123", terminal.output)
            finally:
                self.assertTrue(terminal.close())

    def test_keyboard_menu_stays_next_to_selected_message_after_scroll_and_resize(self):
        with InteractionMock(count=60) as server:
            terminal = self.open(server)
            try:
                terminal.resize(32, 100)
                self.wait(terminal, "Mensagem 0060")
                terminal.send("Preserved draft\x1b[18~m")

                def check_position(message):
                    self.wait(terminal, "Ver quem reagiu")
                    self.wait(terminal, message)
                    rows = terminal.screen.display
                    message_row = next(i for i, line in enumerate(rows) if message in line)
                    menu_row = next(i for i, line in enumerate(rows) if "Ver quem reagiu" in line)
                    self.assertLessEqual(abs(menu_row - message_row), 4, self.screen(terminal))
                    self.assertGreater(
                        rows[menu_row].index("Ver quem reagiu"),
                        rows[message_row].index(message) + len(message),
                        self.screen(terminal),
                    )

                check_position("Mensagem 0060")
                terminal.send("\x1b" + "\x1bOA" * 24 + "m")
                check_position("Mensagem 0036")
                terminal.resize(24, 60)
                check_position("Mensagem 0036")
                terminal.send("m")
                self.assertNotIn("Ver quem reagiu", self.screen(terminal))
                terminal.send("\x1b")
                self.assertIn("Preserved draft", self.editor_text(terminal))
                self.assertEqual(server.message_count, 0)
            finally:
                self.assertTrue(terminal.close())

    def test_manual_retry_keeps_captured_reply_and_preserves_new_target(self):
        with InteractionMock(count=2) as server:
            server.scenario = "send-forbidden"
            terminal = self.open(server)
            try:
                terminal.resize(32, 140)
                self.right_click(terminal, "Mensagem 0001")
                self.click(terminal, "Responder")
                terminal.send("Reply to first message\n")
                self.wait(terminal, "Sem permissão")
                self.right_click(terminal, "Mensagem 0002")
                self.click(terminal, "Responder")
                server.scenario = "public"
                terminal.send("\x14")
                self.wait(terminal, "Mensagem confirmada")
                self.assertEqual(server.sent_messages[0]["reply_to"], server.messages[0]["id"])
                reply_strip = next(
                    line for line in terminal.screen.display if "Respondendo a" in line
                )
                self.assertIn("Mensagem 0002", reply_strip)
                requests = [
                    r for r in server.requests if r[0] == "POST" and r[1].endswith("/messages")
                ]
                self.assertEqual(
                    [r[3]["reply_to"] for r in requests], [server.messages[0]["id"]] * 2
                )
            finally:
                self.assertTrue(terminal.close())

    def test_reply_shortcut_focuses_editor_and_captures_selected_message(self):
        with InteractionMock(count=2) as server:
            terminal = self.open(server)
            try:
                terminal.resize(32, 100)
                self.wait(terminal, "Mensagem 0002")
                terminal.send("Preserved draft\x1b[18~\x1bOAR")
                self.wait(terminal, "Respondendo a")
                self.wait(terminal, "Foco: Mensagem")
                self.assertIn(
                    "Mensagem 0001",
                    next(line for line in terminal.screen.display if "Respondendo a" in line),
                )
                self.assertIn("Preserved draft", self.editor_text(terminal))
                terminal.send("\n")
                self.wait(terminal, "Mensagem confirmada")
                self.assertEqual(server.sent_messages[0]["reply_to"], server.messages[0]["id"])
                self.assertEqual(server.sent_messages[0]["content"], "Preserved draft")
            finally:
                self.assertTrue(terminal.close())

    def test_escape_cancels_reply_and_highlight_without_leaving_editor_or_losing_text(self):
        with InteractionMock(count=2) as server:
            terminal = self.open(server)
            try:
                terminal.resize(32, 100)
                self.wait(terminal, "Mensagem 0002")
                terminal.send("Preserved draft\x1b[18~\x1bOAr")
                self.wait(terminal, "Respondendo a")
                border = next(
                    i
                    for i, line in enumerate(terminal.screen.display)
                    if "↰ Respondendo" in line and "Respondendo a" not in line
                )
                original = next(
                    i
                    for i, line in enumerate(terminal.screen.display)
                    if "Mensagem 0001" in line and "Respondendo a" not in line
                )
                self.assertLess(border, original)
                self.assertLessEqual(original - border, 2)
                self.wait(terminal, "Esc cancelar resposta")
                terminal.send("\x1b")
                self.assertNotIn("Respondendo", self.screen(terminal))
                self.assertIn("Foco: Mensagem", self.screen(terminal))
                self.assertIn("Preserved draft", self.editor_text(terminal))
                terminal.send("\n")
                self.wait(terminal, "Mensagem confirmada")
                self.assertEqual(server.sent_messages[0]["content"], "Preserved draft")
                self.assertIsNone(server.sent_messages[0].get("reply_to"))
                request = next(
                    r for r in server.requests if r[0] == "POST" and r[1].endswith("/messages")
                )
                self.assertNotIn("reply_to", request[3])
            finally:
                self.assertTrue(terminal.close())

    def test_context_menu_does_not_edit_or_send_the_draft_on_paste(self):
        with InteractionMock(count=1) as server:
            terminal = self.open(server)
            try:
                terminal.send("Preserved draft")
                self.right_click(terminal, "Mensagem 0001")
                terminal.send("\x1b[200~Injected paste\nnot a send\x1b[201~")
                self.wait(terminal, "Responder")
                terminal.send("\x1b")
                self.assertIn("Preserved draft", self.editor_text(terminal))
                self.assertNotIn("Injected", self.editor_text(terminal))
                self.assertEqual(server.message_count, 0)
            finally:
                self.assertTrue(terminal.close())

    def test_quote_uses_webhook_author_and_jumps_to_original(self):
        with InteractionMock() as server:
            original = server.messages[0]
            original.update(
                author_id=None,
                webhook_id=uuid(700),
                external_author={"name": "Build Bot"},
                content="Original do webhook",
            )
            server.messages[-1]["reply_to"] = original["id"]
            terminal = self.open(server)
            try:
                terminal.resize(32, 100)
                self.wait(terminal, "↰ Build Bot: Original do webhook")
                self.assertNotIn("Resposta:", self.screen(terminal))
                self.click(terminal, "↰ Build Bot:")
                self.wait(terminal, "↑↓ mensagens")
                self.assertIn("Original do webhook", self.screen(terminal))
                server.emit(
                    dict(
                        type="message_edit",
                        id=original["id"],
                        channel_id=CHANNEL,
                        content="Original editada",
                        edited_at="2026-10-13T00:00:00Z",
                    )
                )
                self.wait(terminal, "↰ Build Bot: Original editada")
                server.emit(dict(type="message_delete", id=original["id"], channel_id=CHANNEL))
                self.wait(terminal, "↰ mensagem não disponível")
            finally:
                self.assertTrue(terminal.close())

    def test_old_original_is_resolved_automatically_without_scrolling(self):
        with InteractionMock(count=320) as server:
            server.messages[0]["content"] = "Original muito antiga"
            server.messages[-1]["reply_to"] = server.messages[0]["id"]
            terminal = self.open(server)
            try:
                self.wait(terminal, "Mensagem 0320")
                self.wait(terminal, "Original muito antiga")
                self.assertIn("Mensagem 0320", self.screen(terminal))
                pages = [r for r in server.requests if r[0] == "GET" and "last_id=" in r[1]]
                self.assertGreaterEqual(len(pages), 3)
                self.assertFalse(any(OTHER in r[1] for r in pages))
                self.click(terminal, "Original muito antiga")
                self.wait(terminal, "↑↓ mensagens")
                self.settled(terminal, lambda: "Mensagem 0320" not in self.screen(terminal))
                self.assertIn("Original muito antiga", self.screen(terminal))
                terminal.resize(20, 60)
                self.assertIn("Original muito antiga", self.screen(terminal))
            finally:
                self.assertTrue(terminal.close())

    def test_quote_lookup_failure_does_not_claim_original_was_deleted(self):
        with InteractionMock(count=220) as server:
            server.messages[-1]["reply_to"] = server.messages[0]["id"]
            server.lookup_status = 503
            terminal = self.open(server)
            try:
                self.wait(terminal, "Falha ao buscar")
                self.assertNotIn("↰ mensagem não disponível", self.screen(terminal))
                count = len([r for r in server.requests if "last_id=" in r[1]])
                terminal.read(0.4)
                self.assertEqual(count, len([r for r in server.requests if "last_id=" in r[1]]))
            finally:
                self.assertTrue(terminal.close())

    def test_failed_jump_retries_in_place_without_automatic_requests(self):
        with InteractionMock(count=220) as server:
            server.messages[0]["content"] = "Original to revisit"
            server.messages[-1]["reply_to"] = server.messages[0]["id"]
            terminal = self.open(server)
            try:
                self.wait(terminal, "Original to revisit")
                server.lookup_status = 503
                self.click(terminal, "Original to revisit")
                self.wait(terminal, "Falha ao carregar")
                count = len([r for r in server.requests if "last_id=" in r[1]])
                terminal.read(0.4)
                self.assertEqual(count, len([r for r in server.requests if "last_id=" in r[1]]))
                self.assertIn("Mensagem 0220", self.screen(terminal))
                server.lookup_status = None
                terminal.send("\x12")
                self.wait(terminal, "↑↓ mensagens")
                self.assertIn("Original to revisit", self.screen(terminal))
                self.assertNotIn("Mensagem 0220", self.screen(terminal))
            finally:
                self.assertTrue(terminal.close())

    def test_click_toggles_own_reaction_without_double_counting_ws(self):
        with InteractionMock(count=1) as server:
            message = server.messages[0]["id"]
            server.seed(message, {"unicode": "☕"}, [uuid(30), uuid(31)])
            terminal = self.open(server)
            try:
                self.wait(terminal, "[☕ 2]")
                terminal.send("Unsaved draft")
                self.click(terminal, "[☕ 2]")
                self.wait(terminal, "[☕ 3]")
                self.settled(terminal, lambda: len(server.reaction_calls) == 1)
                # Allow the authoritative history recovery to finish before the next gesture.
                terminal.read(1.0)
                self.click(terminal, "[☕ 3]")
                self.wait(terminal, "[☕ 2]")
                self.settled(terminal, lambda: len(server.reaction_calls) == 2)
                self.assertEqual([r[0] for r in server.reaction_calls], ["POST", "DELETE"])
                self.assertIn("Unsaved draft", self.editor_text(terminal))
            finally:
                self.assertTrue(terminal.close())

    def test_reaction_picker_does_not_insert_into_draft(self):
        with InteractionMock(count=1) as server:
            terminal = self.open(server)
            try:
                terminal.send("Preserved draft")
                self.wait(terminal, "Mensagem 0001")
                terminal.send("\x1b[18~e")
                self.wait(terminal, "Reagir · Esc voltar")
                self.wait(terminal, "Unicode")
                terminal.send("grinning\x1bOB\x1bOB\n")
                self.settled(terminal, lambda: len(server.reaction_calls) == 1)
                self.settled(terminal, lambda: "Reagir · Esc voltar" not in self.screen(terminal))
                self.assertEqual(server.reaction_calls[0][2]["unicode"], "😀")
                self.assertIn("Preserved draft", self.editor_text(terminal))
                self.assertEqual(server.message_count, 0)
            finally:
                self.assertTrue(terminal.close())

    def test_canceling_reaction_picker_restores_ordinary_emoji_completion(self):
        with InteractionMock(count=1) as server:
            terminal = self.open(server)
            try:
                terminal.send("Preserved draft ")
                self.right_click(terminal, "Mensagem 0001")
                self.click(terminal, "Adicionar reação")
                self.wait(terminal, "Unicode")
                terminal.send("\x1b")
                terminal.send(":grinning\t")
                self.assertEqual(server.reaction_calls, [])
                self.assertEqual(server.message_count, 0)
                self.assertIn("Preserved draft", self.editor_text(terminal))
                self.assertNotIn(":grinning", self.editor_text(terminal))
            finally:
                self.assertTrue(terminal.close())

    def test_reaction_429_is_not_replayed(self):
        with InteractionMock(count=1) as server:
            server.seed(server.messages[0]["id"], {"unicode": "☕"}, [uuid(30)])
            server.reaction_status = 429
            terminal = self.open(server)
            try:
                self.wait(terminal, "[☕ 1]")
                self.click(terminal, "[☕ 1]")
                self.settled(terminal, lambda: len(server.reaction_calls) == 1)
                terminal.read(1.2)
                self.assertEqual(len(server.reaction_calls), 1)
                self.assertIn("[☕ 1]", self.screen(terminal))
            finally:
                self.assertTrue(terminal.close())

    def test_inflight_reaction_does_not_apply_to_another_channel(self):
        with InteractionMock(count=1) as server:
            server.seed(server.messages[0]["id"], {"unicode": "☕"}, [uuid(30)])
            server.reaction_delay = 0.7
            terminal = self.open(server)
            try:
                self.wait(terminal, "[☕ 1]")
                self.click(terminal, "[☕ 1]")
                terminal.send("\x1b\x1bOB\n")
                self.wait(terminal, "Conteúdo do outro canal")
                self.assertNotIn("[☕", self.screen(terminal))
                self.assertEqual(len(server.reaction_calls), 1)
                terminal.send("\x1b\x1bOA\n")
                self.wait(terminal, "[☕ 2]")
            finally:
                self.assertTrue(terminal.close())

    def test_reaction_failures_preserve_counts_without_retries(self):
        for status in (400, 403, 404, 409, 500):
            with self.subTest(status=status), InteractionMock(count=1) as server:
                server.seed(server.messages[0]["id"], {"unicode": "☕"}, [uuid(30)])
                server.reaction_status = status
                terminal = self.open(server)
                try:
                    self.wait(terminal, "[☕ 1]")
                    self.click(terminal, "[☕ 1]")
                    self.settled(terminal, lambda: len(server.reaction_calls) == 1)
                    terminal.read(0.2)
                    self.assertIn("[☕ 1]", self.screen(terminal))
                    self.assertEqual(len(server.reaction_calls), 1)
                finally:
                    self.assertTrue(terminal.close())

    def test_participants_load_multiple_groups_and_pages(self):
        with InteractionMock(count=1) as server:
            message = server.messages[0]["id"]
            for i in range(103):
                server.seed(message, {"unicode": "☕" if i % 2 else "☀"}, [uuid(30 + i)])
            terminal = self.open(server)
            try:
                self.wait(terminal, "Mensagem 0001")
                terminal.send("\x1b[18~E")
                self.wait(terminal, "Quem reagiu")
                self.wait(terminal, "Autor")
                terminal.send("\x1bOC" + "\x1b[6~" * 14)
                self.wait(terminal, "Fim da lista")
                pages = [r for r in server.requests if r[0] == "GET" and "/reactions?" in r[1]]
                self.assertEqual(len(pages), 2)
                self.assertIn("last_id=", pages[1][1])
                terminal.send("\x1b")
                self.assertNotIn("Quem reagiu", self.screen(terminal))
            finally:
                self.assertTrue(terminal.close())

    def test_participant_columns_select_filter_and_scroll_independently(self):
        with InteractionMock(count=1) as server:
            emojis = [
                "☀",
                "☕",
                "♥",
                "⚡",
                "❄",
                "☃",
                "♠",
                "♣",
                "♦",
                "♨",
                "⚽",
                "⛳",
                "✅",
                "❌",
                "⭐",
                "⌚",
                "⌛",
                "☎",
                "⚓",
                "☂",
            ]
            message = server.messages[0]["id"]
            server.seed(message, {"unicode": emojis[0]}, [uuid(300 + i) for i in range(35)])
            for i, emoji in enumerate(emojis[1:], 1):
                server.seed(message, {"unicode": emoji}, [uuid(400 + i)])
            parent = server.route

            def route(method, path, body):
                if method == "POST" and path.endswith("/users/user_summary_batch"):
                    return 200, [
                        {"id": id, "username": f"Pessoa {int(id[-12:], 16):04d}", "nickname": None}
                        for id in body["ids"]
                    ]
                return parent(method, path, body)

            server.api_handler = route
            terminal = self.open(server)
            try:
                terminal.resize(32, 100)
                self.wait(terminal, "Mensagem 0001")
                terminal.send("Preserved draft\x1b[18~E")
                self.wait(terminal, "Pessoa 0334")
                self.wait(terminal, "Fim da lista")

                def columns():
                    display = terminal.screen.display
                    header = next(
                        i
                        for i, line in enumerate(display)
                        if "Reações" in line and "Pessoas" in line
                    )
                    end = next(
                        i for i in range(header, len(display)) if "Fim da lista" in display[i]
                    )
                    left, right = display[header].index("Reações"), display[header].index("Pessoas")
                    return (
                        header,
                        left,
                        right,
                        [
                            (
                                "".join(
                                    terminal.screen.buffer[row][col].data
                                    for col in range(left, right)
                                ),
                                "".join(
                                    terminal.screen.buffer[row][col].data
                                    for col in range(right, terminal.screen.columns)
                                ),
                            )
                            for row in range(header + 2, end - 1)
                        ],
                    )

                def people():
                    rows = columns()[3]
                    return [
                        match[0] for line in rows if (match := re.findall(r"Pessoa \d+", line[1]))
                    ]

                header, left, right, rows = columns()
                initial_left = [line[0] for line in rows]
                initial_people = people()
                self.assertIn("☀ 35", "\n".join(initial_left))
                self.assertNotIn("☂", "\n".join(initial_left))
                terminal.send(f"\x1b[<65;{right + 3};{header + 4}M")
                self.assertNotEqual(people(), initial_people)
                self.assertEqual([line[0] for line in columns()[3]], initial_left)
                right_scrolled = people()
                terminal.send("\x1bOD")
                for _ in range(3):
                    terminal.send(f"\x1b[<65;{left + 3};{header + 4}M")
                self.assertIn("☂", "\n".join(line[0] for line in columns()[3]))
                self.assertEqual(people(), right_scrolled)
                terminal.send("\x1bOB" * 13 + "\n")
                self.wait(terminal, "Pessoa 0419")
                self.assertEqual(people(), ["Pessoa 0419"])
                terminal.send("\x1bOD")
                header, left, right, rows = columns()
                row = next(i for i, line in enumerate(rows) if "♠" in line[0])
                terminal.click(header + 2 + row, left + 1)
                self.wait(terminal, "Pessoa 0406")
                self.assertEqual(people(), ["Pessoa 0406"])
                terminal.send("\x1bOC\x1bOA\x1bOD")
                self.assertEqual(people(), ["Pessoa 0406"])
                terminal.resize(18, 48)
                self.wait(terminal, "Pessoa 0406")
                self.assertEqual(people(), ["Pessoa 0406"])
                terminal.send("\x1b\x1b")
                self.assertIn("Preserved draft", self.editor_text(terminal))
                pages = [r for r in server.requests if r[0] == "GET" and "/reactions?" in r[1]]
                self.assertEqual(len(pages), 1)
                self.assertEqual(server.reaction_calls, [])
                self.assertEqual(server.message_count, 0)
            finally:
                self.assertTrue(terminal.close())

    def test_participant_failure_waits_for_explicit_retry(self):
        with InteractionMock(count=1) as server:
            server.seed(server.messages[0]["id"], {"unicode": "☕"}, [uuid(30)])
            server.reactor_status = 503
            terminal = self.open(server)
            try:
                self.wait(terminal, "Mensagem 0001")
                terminal.send("\x1b[18~E")
                self.wait(terminal, "Falha ao listar")
                terminal.read(0.4)

                def pages():
                    return [r for r in server.requests if r[0] == "GET" and "/reactions?" in r[1]]

                self.assertEqual(len(pages()), 1)
                server.reactor_status = None
                terminal.send("\x12")
                self.wait(terminal, "Autor e")
                self.wait(terminal, "Fim da lista")
                self.assertEqual(len(pages()), 2)
            finally:
                self.assertTrue(terminal.close())

    def test_older_selected_emoji_loads_missing_participant_pages_automatically(self):
        with InteractionMock(count=1) as server:
            message = server.messages[0]["id"]
            server.seed(message, {"unicode": "☀"}, [uuid(30)])
            server.seed(message, {"unicode": "☕"}, [uuid(100 + i) for i in range(120)])
            terminal = self.open(server)
            try:
                self.wait(terminal, "Mensagem 0001")
                terminal.send("\x1b[18~E")
                self.wait(terminal, "Autor e")
                self.wait(terminal, "Fim da lista")
                self.assertIn("☀ 1", self.screen(terminal))
                self.assertIn("☕ 120", self.screen(terminal))
                pages = [r for r in server.requests if r[0] == "GET" and "/reactions?" in r[1]]
                self.assertEqual(len(pages), 2)
                terminal.read(0.3)
                self.assertEqual(
                    len([r for r in server.requests if r[0] == "GET" and "/reactions?" in r[1]]), 2
                )
            finally:
                self.assertTrue(terminal.close())

    def test_complete_selected_group_does_not_prefetch_other_groups(self):
        with InteractionMock(count=1) as server:
            message = server.messages[0]["id"]
            server.seed(message, {"unicode": "☕"}, [uuid(100 + i) for i in range(200)])
            server.seed(message, {"unicode": "☀"}, [uuid(30)])
            terminal = self.open(server)
            try:
                self.wait(terminal, "Mensagem 0001")
                terminal.send("\x1b[18~E")
                self.wait(terminal, "Role para carregar mais")
                terminal.send("\x1bOB\n")
                self.wait(terminal, "Autor e")
                terminal.read(0.4)
                pages = [r for r in server.requests if r[0] == "GET" and "/reactions?" in r[1]]
                self.assertEqual(len(pages), 1)
                self.assertIn("☕ 200", self.screen(terminal))
                self.assertIn("☀ 1", self.screen(terminal))
            finally:
                self.assertTrue(terminal.close())

    def test_participant_panel_closes_when_history_recovers_after_reconnect(self):
        with InteractionMock(count=1) as server:
            server.seed(server.messages[0]["id"], {"unicode": "☕"}, [uuid(30)])
            terminal = self.open(server)
            try:
                self.right_click(terminal, "Mensagem 0001")
                self.click(terminal, "Ver quem reagiu")
                self.wait(terminal, "Quem reagiu")
                self.wait(terminal, "Autor")
                server.disconnect()
                self.settled(
                    terminal, lambda: "Quem reagiu" not in self.screen(terminal), seconds=10
                )
                self.wait(terminal, "Mensagem 0001")
            finally:
                self.assertTrue(terminal.close())

    def test_ws_absolute_counts_remove_empty_groups_and_reject_invalid_emoji(self):
        with InteractionMock(count=1) as server:
            terminal = self.open(server)
            try:
                self.wait(terminal, "Mensagem 0001")
                event = dict(
                    type="react_update",
                    message_id=server.messages[0]["id"],
                    emoji_id=None,
                    unicode="☕",
                    count=4,
                )
                server.emit(event)
                self.wait(terminal, "[☕ 4]")
                server.emit(dict(event, count=0))
                self.settled(terminal, lambda: "[☕" not in self.screen(terminal))
                server.emit(dict(event, unicode="\x1b]52;c;INJECT\x07"))
                terminal.read(0.2)
                self.assertNotIn(b"\x1b]52;c;INJECT", terminal.output)
                server.emit(dict(event, count=-1))
                terminal.read(0.1)
                self.assertNotIn("[☕", self.screen(terminal))
            finally:
                self.assertTrue(terminal.close())


if __name__ == "__main__":
    unittest.main()
