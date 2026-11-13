"""Ctrl+K conversation switcher through the real TUI with synthetic servers."""

from test_context import main as test_main

import time
import unittest
from history_mock import OTHER, STAMP, HistoryMock, uuid
from terminal_support import Terminal, wait_for_screen

AUTHOR = uuid(301)


class QuickSwitcher(unittest.TestCase):
    def screen(self, terminal):
        return "\n".join(terminal.screen.display)

    def wait(self, terminal, text):
        deadline = time.monotonic() + 8
        while text not in self.screen(terminal) and time.monotonic() < deadline:
            terminal.read(0.03)
        self.assertIn(text, self.screen(terminal))

    def gone(self, terminal, text):
        wait_for_screen(terminal, lambda: text not in self.screen(terminal), f"{text!r} stayed")

    def open(self, server):
        terminal = Terminal()
        self.addCleanup(lambda: terminal.close() if terminal.process.poll() is None else None)
        terminal.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(terminal, "Mensagem 0250")
        return terminal

    def rows(self, terminal):
        """Rows of the open switcher's list, between its rule and its hint line."""
        display = terminal.screen.display
        title = next(i for i, line in enumerate(display) if "Ir para conversa" in line)
        hint = next(i for i, line in enumerate(display) if "Enter: abrir" in line)
        # The window's top corners bound its interior; the history around it
        # stays visible. Line drawing reaches the observer as l, k, x and q.
        top = display[title - 1]
        left = display[title].index("Ir para conversa") - 2
        right = top.index("k", left + 1)
        inner = (line[left + 1 : right] for line in display[title + 3 : hint])
        return [line for line in inner if line.strip()]

    def test_filters_by_name_opens_the_conversation_and_esc_keeps_the_draft(self):
        with HistoryMock() as server:
            server.channels.append(
                {"id": uuid(77), "name": "Ação", "position": 5, "topic": None, "type": "text"}
            )
            t = self.open(server)
            t.send("\x1bOB\n")
            self.wait(t, "# geral · Histórico")
            t.send("\t")
            self.wait(t, "Foco: Mensagem")
            t.send("Rascunho do geral")
            self.wait(t, "Rascunho do geral")
            t.send("\x0b")
            self.wait(t, "Ir para conversa")
            rows = "\n".join(self.rows(t))
            # Every loaded conversation but the one on screen and categories.
            self.assertIn("# outro", rows)
            self.assertIn("♪ voz", rows)
            self.assertIn("# Ação", rows)
            self.assertNotIn("# geral", rows)
            self.assertNotIn("Categoria", rows)
            self.assertIn("Comunidade", rows)
            # Accents and case do not matter.
            t.send("ACAO")
            wait_for_screen(t, lambda: len(self.rows(t)) == 1, "Filter did not narrow the list")
            self.assertIn("# Ação", self.rows(t)[0])
            t.send("\x15zzz")
            self.wait(t, "Nenhuma conversa encontrada.")
            t.send("\x15OUT")
            wait_for_screen(t, lambda: len(self.rows(t)) == 1, "Filter did not find outro")
            t.send("\n")
            self.wait(t, "Conteúdo do outro canal")
            self.gone(t, "Ir para conversa")
            # The typed text went to the search, never into the outro draft.
            self.assertNotIn("OUT", t.screen.display[-4] + t.screen.display[-3])
            t.send("\x0b")
            self.wait(t, "Ir para conversa")
            self.assertNotIn("# outro", "\n".join(self.rows(t)))
            t.send("\x1b")
            self.gone(t, "Ir para conversa")
            self.wait(t, "Conteúdo do outro canal")
            t.send("\x0bgeral\n")
            self.wait(t, "Mensagem 0250")
            self.wait(t, "Rascunho do geral")
            self.assertEqual(server.sent_messages, [])

    def test_mentions_and_unread_conversations_come_first(self):
        with HistoryMock() as server:
            server.channels.append(
                {
                    "id": uuid(78),
                    "name": "zebra",
                    "position": 6,
                    "topic": None,
                    "type": "text",
                    "last_message": {
                        "id": uuid(7000),
                        "author_id": AUTHOR,
                        "created_at": STAMP,
                        "content": "novidade",
                    },
                    "last_read_message": None,
                }
            )
            t = self.open(server)
            t.send("\x0b")
            self.wait(t, "Ir para conversa")
            rows = self.rows(t)
            self.assertIn("# zebra", rows[0])
            self.assertIn("⦁", rows[0])
            self.assertNotIn("⦁", "\n".join(rows[1:]))
            # Empty search: Enter opens the first one, the unread conversation.
            t.send("\n")
            self.gone(t, "Ir para conversa")
            wait_for_screen(
                t,
                lambda: any(f"/channels/{uuid(78)}/messages" in r[1] for r in server.requests),
                "Enter did not open the unread conversation",
            )

    def test_resize_click_outside_and_ctrl_k_close_without_changes(self):
        with HistoryMock() as server:
            t = self.open(server)
            t.send("\x0bout")
            self.wait(t, "Ir para conversa")
            t.resize(18, 60)
            self.wait(t, "Ir para conversa")
            self.wait(t, "# outro")
            t.resize(40, 140)
            self.wait(t, "Buscar: out")
            t.send("\x0b")
            self.gone(t, "Ir para conversa")
            self.wait(t, "Mensagem 0250")
            t.send("\x0b")
            self.wait(t, "Ir para conversa")
            t.click(0, 0)
            self.gone(t, "Ir para conversa")
            self.wait(t, "Mensagem 0250")
            self.assertFalse(any(f"/channels/{OTHER}/messages" in r[1] for r in server.requests))

    def test_conversation_of_another_server_switches_its_tab(self):
        with HistoryMock() as first, HistoryMock() as second:
            second.name = "Trabalho"
            for channel in second.channels:
                if channel["id"] == OTHER:
                    channel["name"] = "planos"
            t = self.open(first)
            row = t.screen.display[0]
            t.click(0, row.index("[+ Adicionar]") + 2)
            self.wait(t, "URL do servidor")
            t.send(second.url + "\tjosé\tSynthetic!123\n")
            self.wait(t, "Trabalho")
            self.wait(t, "Mensagem 0250")
            # Back to the first server, then jump to the second one by name.
            t.click(0, t.screen.display[0].index("Comunidade") + 1)
            wait_for_screen(
                t, lambda: "# planos" not in self.screen(t), "Did not return to Comunidade"
            )
            before = sum(f"/channels/{OTHER}/messages" in r[1] for r in second.requests)
            t.send("\x0bplanos")
            self.wait(t, "Ir para conversa")
            wait_for_screen(t, lambda: len(self.rows(t)) == 1, "Filter did not find planos")
            self.assertIn("Trabalho", self.rows(t)[0])
            t.send("\n")
            self.gone(t, "Ir para conversa")
            self.wait(t, "Conteúdo do outro canal")
            wait_for_screen(
                t,
                lambda: (
                    sum(f"/channels/{OTHER}/messages" in r[1] for r in second.requests) > before
                ),
                "The second server did not load planos",
            )
            self.assertNotIn("Mensagem não disponível", self.screen(t))


if __name__ == "__main__":
    test_main()
