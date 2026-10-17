"""Permission changes through the real TUI with synthetic HTTP and WS data."""

import time
import unittest
from urllib.parse import urlsplit

from history_mock import CHANNEL, OTHER, uuid
from interaction_mock import InteractionMock
import test_message_interactions as interactions

ROLE = uuid(400)
READ_ROLE = uuid(401)


class PermissionMock(InteractionMock):
    def __init__(self, restricted=False):
        super().__init__(count=2)
        self.owner_id = uuid(99)
        self.roles = [
            {"id": ROLE, "name": "Moderator", "permissions": {"manage_roles": True}},
            {"id": READ_ROLE, "name": "Reader", "permissions": {}},
        ]
        self.role_status = None
        self.user_roles = [{"id": READ_ROLE}] if restricted else []
        for channel in self.channels:
            channel["permissions"] = []
            if restricted and channel["id"] == CHANNEL:
                channel["permissions"] = [
                    {"role_id": READ_ROLE, "permissions": {"read_channel": True}},
                    {
                        "role_id": ROLE,
                        "permissions": {
                            "read_channel": True,
                            "send_messages": True,
                            "delete_messages": True,
                        },
                    },
                ]
            elif channel["id"] == OTHER:
                channel["permissions"] = [
                    {"role_id": ROLE, "permissions": {"read_channel": True, "send_messages": True}},
                ]

    def role(self, role, add):
        self.user_roles = [item for item in self.user_roles if item["id"] != role]
        if add:
            self.user_roles.append({"id": role})
        self.emit(
            {"type": "role_add" if add else "role_remove", "user_id": uuid(1), "role_id": role}
        )

    def route(self, method, path, body):
        if method == "GET" and urlsplit(path).path.endswith("/roles") and self.role_status:
            return self.role_status, None
        return super().route(method, path, body)


class PermissionsTerminal(unittest.TestCase):
    screen = interactions.MessageInteractions.screen

    def wait(self, terminal, text, seconds=6):
        self.settled(terminal, lambda: text in self.screen(terminal), seconds=seconds)

    editor_text = interactions.MessageInteractions.editor_text
    click = interactions.MessageInteractions.click
    settled = interactions.MessageInteractions.settled
    right_click = interactions.MessageInteractions.right_click
    open = interactions.MessageInteractions.open

    def test_regular_author_sees_only_permitted_message_actions(self):
        with PermissionMock() as server:
            server.messages[0]["author_id"] = uuid(1)
            terminal = self.open(server)
            try:
                self.right_click(terminal, "Mensagem 0002")
                self.wait(terminal, "Ver quem reagiu")
                self.assertNotIn("Excluir mensagem", self.screen(terminal))
                self.assertNotIn("Editar mensagem", self.screen(terminal))
                terminal.send("\x1b")
                self.right_click(terminal, "Mensagem 0001")
                self.wait(terminal, "Editar mensagem")
                self.wait(terminal, "Excluir mensagem")
                self.assertEqual(server.mutation_calls, [])
            finally:
                self.assertTrue(terminal.close())

    def test_ws_grant_reveals_hidden_channel_and_revocation_updates_open_menu(self):
        with PermissionMock(restricted=True) as server:
            terminal = self.open(server)
            try:
                self.assertNotIn("outro", self.screen(terminal))
                self.right_click(terminal, "Mensagem 0002")
                self.wait(terminal, "Ver quem reagiu")
                self.assertNotIn("Excluir mensagem", self.screen(terminal))
                started = time.monotonic()
                server.role(ROLE, True)
                self.wait(terminal, "Excluir mensagem", seconds=3)
                self.assertLess(time.monotonic() - started, 3)
                self.assertNotIn("Editar mensagem", self.screen(terminal))
                self.wait(terminal, "outro")
                # Enter the destructive confirmation, then revoke while it is open.
                self.click(terminal, "Excluir mensagem")
                self.wait(terminal, "Cancelar")
                server.role(ROLE, False)
                self.settled(
                    terminal, lambda: "Excluir mensagem" not in self.screen(terminal), seconds=3
                )
                self.assertEqual(server.mutation_calls, [])
                self.assertNotIn("outro", self.screen(terminal))
            finally:
                self.assertTrue(terminal.close())

    def test_read_only_channel_keeps_own_edit_and_reaction_removal(self):
        with PermissionMock(restricted=True) as server:
            message = server.messages[0]
            message["author_id"] = uuid(1)
            server.seed(message["id"], {"unicode": "☕"}, [uuid(1), uuid(30)])
            terminal = self.open(server)
            try:
                terminal.send("Preserved read-only draft\n")
                self.wait(terminal, "Sem permissão")
                self.assertEqual(server.message_count, 0)
                self.assertIn("Preserved read-only draft", self.editor_text(terminal))
                self.right_click(terminal, "Mensagem 0001")
                self.wait(terminal, "Editar mensagem")
                self.assertNotIn("Adicionar reação", self.screen(terminal))
                self.assertNotIn("Responder", self.screen(terminal))
                terminal.send("\x1b")
                self.click(terminal, "☕ 2")
                self.settled(terminal, lambda: len(server.reaction_calls) == 1)
                self.assertEqual(server.reaction_calls[0][0], "DELETE")
                self.click(terminal, "☕ 1")
                terminal.read(0.3)
                self.assertEqual(len(server.reaction_calls), 1)
            finally:
                self.assertTrue(terminal.close())

    def test_losing_read_access_closes_history_preserves_draft_and_grant_reveals_channel(self):
        with PermissionMock(restricted=True) as server:
            terminal = self.open(server)
            try:
                terminal.send("Draft survives access changes")
                server.role(READ_ROLE, False)
                self.settled(
                    terminal, lambda: "Mensagem 0002" not in self.screen(terminal), seconds=3
                )
                self.assertNotIn("Conversar em #geral", self.screen(terminal))
                reads = len([r for r in server.requests if "/messages" in r[1] and r[0] == "GET"])
                terminal.read(0.3)
                self.assertEqual(
                    reads,
                    len([r for r in server.requests if "/messages" in r[1] and r[0] == "GET"]),
                )
                server.role(READ_ROLE, True)
                self.wait(terminal, "geral", seconds=3)
                self.click(terminal, "geral")
                self.wait(terminal, "# geral · Histórico")
                self.assertIn("Draft survives access changes", self.editor_text(terminal))
            finally:
                self.assertTrue(terminal.close())

    def test_events_for_other_accounts_do_not_grant_access(self):
        with PermissionMock(restricted=True) as server:
            terminal = self.open(server)
            try:
                self.right_click(terminal, "Mensagem 0002")
                self.wait(terminal, "Ver quem reagiu")
                server.emit({"type": "role_add", "user_id": uuid(2), "role_id": ROLE})
                terminal.read(0.3)
                self.assertNotIn("Excluir mensagem", self.screen(terminal))
                self.assertNotIn("Adicionar reação", self.screen(terminal))
                self.assertEqual(server.mutation_calls, [])
            finally:
                self.assertTrue(terminal.close())


if __name__ == "__main__":
    unittest.main()
