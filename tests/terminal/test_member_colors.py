"""Member colors through ncurses, using synthetic REST and WebSocket peers."""

import copy
import unittest
from wcwidth import wcswidth
from urllib.parse import urlsplit

from history_mock import STAMP, uuid
from interaction_mock import InteractionMock
import test_message_interactions as interactions
from test_terminal import Terminal

RED, GREEN = uuid(400), uuid(401)


class ColorMock(InteractionMock):
    def __init__(self):
        super().__init__(count=3)
        self.roles = [
            dict(id=RED, name="Red", color="#ff0000", permissions={}),
            dict(id=GREEN, name="Green", color="#00ff00", permissions={}),
        ]
        self.user_roles = [dict(id=RED, color="#ff0000")]
        self.members = {
            uuid(21): dict(
                id=uuid(21), username="redmember", nickname="Red Member", roles=[dict(id=RED)]
            ),
            uuid(22): dict(id=uuid(22), username="whitemember", nickname="White Member", roles=[]),
            uuid(20): dict(
                id=uuid(20), username="greenmember", nickname="Green Member", roles=[dict(id=GREEN)]
            ),
        }

    def route(self, method, path, body):
        resource = urlsplit(path).path
        if method == "POST" and resource.endswith("/users/user_summary_batch"):
            self.summary_calls += 1
            return 200, [
                copy.deepcopy(self.members[id]) for id in body["ids"] if id in self.members
            ]
        if method == "GET" and resource.endswith("/users"):
            return 200, dict(
                users=[dict(user, created_at=STAMP) for user in self.members.values()],
                has_more=False,
            )
        return super().route(method, path, body)

    def member_role(self, user, role, add):
        member = self.members[user]
        member["roles"] = [item for item in member["roles"] if item["id"] != role]
        if add:
            member["roles"].append(dict(id=role))
        self.emit(dict(type="role_add" if add else "role_remove", user_id=user, role_id=role))


class MemberColors(unittest.TestCase):
    screen = interactions.MessageInteractions.screen
    wait = interactions.MessageInteractions.wait
    settled = interactions.MessageInteractions.settled
    right_click = interactions.MessageInteractions.right_click
    click = interactions.MessageInteractions.click

    def open(self, server, terminal_type="xterm-direct"):
        terminal = Terminal(terminal_type=terminal_type)
        try:
            terminal.resize(32, 110)
            terminal.send(server.url + "\tjosé\tSynthetic!123\n")
            self.wait(terminal, "geral")
            terminal.send("\x1bOB\n")
            self.wait(terminal, "Mensagem 0003")
            self.wait(terminal, "Red Member")
            return terminal
        except BaseException:
            terminal.close()
            raise

    def cells(self, terminal, label):
        result = []
        for y, line in enumerate(terminal.screen.display):
            if label in line:
                x = wcswidth(line[: line.index(label)])
                result.append(
                    [terminal.screen.buffer[y][x + offset] for offset in range(len(label))]
                )
        return result

    def assert_color(self, terminal, label, color):
        self.settled(
            terminal,
            lambda: (
                bool(self.cells(terminal, label))
                and all(cell.fg == color for cells in self.cells(terminal, label) for cell in cells)
            ),
        )

    def test_names_default_white_and_live_membership_keeps_history_and_draft(self):
        for term in ("xterm-direct", "xterm-256color"):
            with self.subTest(term=term), ColorMock() as server:
                terminal = self.open(server, term)
                try:
                    self.assert_color(terminal, "Red Member", "ff0000")
                    self.assert_color(terminal, "White Member", "ffffff")
                    self.assert_color(terminal, "Green Member", "00ff00")
                    self.assert_color(terminal, "josé", "ff0000")
                    terminal.send("Preserved draft")
                    reads = sum("/messages" in r[1] for r in server.requests if r[0] == "GET")
                    server.member_role(uuid(21), RED, False)
                    self.assert_color(terminal, "Red Member", "ffffff")
                    server.member_role(uuid(21), GREEN, True)
                    self.assert_color(terminal, "Red Member", "00ff00")
                    server.emit(
                        dict(
                            type="presence_update",
                            user_id=uuid(21),
                            status="online",
                            nickname="Renamed Member",
                        )
                    )
                    self.assert_color(terminal, "Renamed Member", "00ff00")
                    self.assertIn("Preserved draft", self.screen(terminal))
                    self.assertEqual(
                        reads, sum("/messages" in r[1] for r in server.requests if r[0] == "GET")
                    )
                finally:
                    self.assertTrue(terminal.close())

    def test_mention_and_member_selectors_keep_colors_and_selection_background(self):
        with ColorMock() as server:
            terminal = self.open(server)
            try:
                terminal.send("@red")
                self.wait(terminal, "Menções")
                self.assert_color(terminal, "Red Member", "ff0000")
                matching = self.cells(terminal, "Red Member")
                self.assertGreaterEqual(len(matching), 2)
                self.assertGreater(len({cell.bg for row in matching for cell in row}), 1)
                server.member_role(uuid(21), RED, False)
                self.assert_color(terminal, "Red Member", "ffffff")
                terminal.resize(28, 90)
                self.assert_color(terminal, "Red Member", "ffffff")
                terminal.send("\x1b\x15\x1b[21~")
                self.wait(terminal, "Membros (")
                self.assert_color(terminal, "White Member", "ffffff")
                self.assert_color(terminal, "Green Member", "00ff00")
                terminal.send("\x1b")
                self.wait(terminal, "Conversar em #geral")
            finally:
                self.assertTrue(terminal.close())

    def test_reaction_members_and_role_definition_refresh(self):
        with ColorMock() as server:
            server.seed(server.messages[0]["id"], {"unicode": "☕"}, [uuid(21), uuid(22)])
            terminal = self.open(server)
            try:
                self.right_click(terminal, "Mensagem 0001")
                self.click(terminal, "Ver quem reagiu")
                self.wait(terminal, "Pessoas")
                self.assert_color(terminal, "Red Member", "ff0000")
                self.assert_color(terminal, "White Member", "ffffff")
                server.roles[0]["color"] = "#0000ff"
                # There is no role-edit event. Any membership event requests
                # the existing role catalog without reading inactive histories.
                server.member_role(uuid(22), GREEN, True)
                self.assert_color(terminal, "Red Member", "0000ff")
                self.assert_color(terminal, "White Member", "00ff00")
                terminal.resize(28, 90)
                self.assert_color(terminal, "Red Member", "0000ff")
                terminal.send("\x1b")
                self.wait(terminal, "Conversar em #geral")
                self.assert_color(terminal, "Red Member", "0000ff")
            finally:
                self.assertTrue(terminal.close())

    def test_webhook_names_use_white_without_borrowing_member_role(self):
        with ColorMock() as server:
            server.messages[-1].update(
                webhook_id=uuid(700), external_author=dict(name="Webhook Member")
            )
            terminal = self.open(server)
            try:
                self.assert_color(terminal, "Webhook Member", "ffffff")
                self.assertIn("[APP]", self.screen(terminal))
                self.assert_color(terminal, "Red Member", "ff0000")
            finally:
                self.assertTrue(terminal.close())


if __name__ == "__main__":
    unittest.main()
