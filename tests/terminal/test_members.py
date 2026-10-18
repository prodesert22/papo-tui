"""Member drawer and actions through the real TUI and synthetic REST/WS."""

import base64
import json
import os
import re
import threading
import time
from pathlib import Path
import unittest

from history_mock import uuid
from member_mock import MemberMock, AUTHOR
from r8_mock import dm
from test_terminal import Terminal
import test_message_interactions as interactions


class Members(unittest.TestCase):
    screen = interactions.MessageInteractions.screen
    wait = interactions.MessageInteractions.wait
    settled = interactions.MessageInteractions.settled

    def click(self, terminal, text):
        if text == "Mensagem":
            self.wait(terminal, "Ver perfil")
            row = next(i for i, line in enumerate(terminal.screen.display) if "Ver perfil" in line)
            column = terminal.screen.display[row].index("Ver perfil")
            terminal.click(row + 1, column + 1)
        else:
            interactions.MessageInteractions.click(self, terminal, text)

    right_click = interactions.MessageInteractions.right_click

    def open(self, server, *, columns=120, mode="ultralight", environment=None):
        terminal = Terminal(environment=environment)
        self.addCleanup(terminal.close)
        terminal.resize(36, columns)
        terminal.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(terminal, "Conversar em #geral")
        # Choose the textual mode so assertions do not depend on a font.
        terminal.send("\x1bOQ")
        self.wait(terminal, "Configurações")
        terminal.send("\t" + "\x1bOB" * (2 if mode == "ultralight" else 1) + "\n\t\n")
        self.settled(terminal, lambda: "Configurações" not in self.screen(terminal))
        self.wait(terminal, "Conversar em #geral")
        return terminal

    def drawer(self, terminal):
        terminal.send("\x1b[21~")
        self.wait(terminal, "Membros (")
        self.wait(terminal, "Equipe")

    def search(self, terminal, text):
        terminal.send("/" + text + "\n")
        self.settled(terminal, lambda: "Membros (1)" in self.screen(terminal))

    def menu(self, terminal, text="Bia", *, mouse=False):
        self.search(terminal, "bob" if text == "Bia" else text)
        if mouse:
            self.right_click(terminal, text)
        else:
            terminal.send("m")
        self.wait(terminal, "Gerar link de reset de senha")

    def test_drawer_search_groups_status_resize_and_draft(self):
        with MemberMock() as server:
            terminal = self.open(server)
            self.click(terminal, "Conversar em #geral")
            terminal.send("rascunho preservado")
            self.drawer(terminal)
            self.wait(terminal, "Comunidade")
            # Bia belongs to two roles but appears only under the first.
            rows = [line for line in terminal.screen.display if "Bia" in line]
            self.assertEqual(len(rows), 1)
            self.click(terminal, "Agrupar: Cargo")
            self.wait(terminal, "Agrupar: Status")
            path = Path(terminal.configuration.name, "papo-tui", "config.json")
            self.assertTrue(json.loads(path.read_text())["members_by_status"])
            server.emit(
                dict(
                    type="presence_sync",
                    members=[dict(user_id=AUTHOR, status="online", status_message="Agora online")],
                )
            )
            self.wait(terminal, "Online")
            self.search(terminal, "@BOB")
            self.wait(terminal, "Bia")
            terminal.resize(26, 60)
            self.wait(terminal, "Membros (1)")
            terminal.send("\x1b[21~")
            self.wait(terminal, "rascunho preservado")

    def test_header_icons_and_arrow_path_to_members_at_wide_and_narrow_sizes(self):
        with MemberMock() as server:
            terminal = self.open(server)
            self.click(terminal, "Conversar em #geral")
            terminal.send("\x1b\x1bOA")
            self.wait(terminal, "Foco: Buscar")
            terminal.send("\x1bOC")
            self.wait(terminal, "Foco: Fixadas")
            terminal.send("\x1bOC")
            self.wait(terminal, "Foco: Membros")
            terminal.send("\n")
            self.wait(terminal, "Membros (6)")
            terminal.send("\x1bOD\x1bOD\n")
            self.wait(terminal, "Mensagens fixadas")
            terminal.send("\x1b\x1bOD\n")
            self.wait(terminal, "Buscar mensagens no servidor")
            terminal.send("\x1b\x1bOC\x1bOC")
            self.wait(terminal, "Foco: Membros")
            terminal.send("\x1b[21~")
            self.settled(terminal, lambda: "Membros (" not in self.screen(terminal))
            terminal.resize(26, 70)
            self.wait(terminal, "🔍")
            self.wait(terminal, "🖈")
            self.wait(terminal, "👤")
            terminal.send("\n")
            self.wait(terminal, "Membros (6)")
            terminal.send("\x1bOD")
            self.wait(terminal, "Foco: Membros")
            self.assertNotIn("Membros (", self.screen(terminal))
            self.assertNotIn("👤", terminal.screen.display[1])

    def test_first_menu_uses_loaded_member_data_and_has_no_empty_bottom_rows(self):
        with MemberMock() as server:
            terminal = self.open(server)
            self.drawer(terminal)
            self.search(terminal, "bob")
            before = len(server.requests)
            self.right_click(terminal, "Bia")
            self.wait(terminal, "Gerar link de reset de senha")
            self.assertFalse(
                any(
                    path.endswith(
                        ("/blocks", "/user_summary_batch", "/roles", "/server", "/auth/whoami")
                    )
                    for _, path, *_ in server.requests[before:]
                )
            )
            row = next(
                i
                for i, line in enumerate(terminal.screen.display)
                if "Gerar link de reset de senha" in line
            )
            column = terminal.screen.display[row].index("Gerar link de reset de senha") - 2
            self.assertIn(terminal.screen.buffer[row + 1][column].data, ("m", "└"))

    def test_slow_initial_block_list_updates_without_input_and_first_click_works(self):
        with MemberMock() as server:
            ready = threading.Event()
            self.addCleanup(ready.set)
            original = server.api_handler

            def route(method, path, body):
                if method == "GET" and path.endswith("/blocks"):
                    ready.wait(12)
                return original(method, path, body)

            server.api_handler = route
            terminal = self.open(server)
            self.drawer(terminal)
            self.search(terminal, "bob")
            self.right_click(terminal, "Bia")
            self.wait(terminal, "Consultando opções")
            self.assertNotIn("Ver perfil", self.screen(terminal))
            ready.set()
            self.wait(terminal, "Bloquear")
            self.click(terminal, "Bloquear")
            self.wait(terminal, "Desbloquear")
            self.assertEqual(server.member_calls, [("POST", AUTHOR, "block", None)])

    def test_keyboard_member_menu_opens_beside_selected_member(self):
        with MemberMock() as server:
            terminal = self.open(server)
            self.drawer(terminal)
            terminal.send("\x1bOF")
            self.wait(terminal, "Zed")
            target_row = next(i for i, line in enumerate(terminal.screen.display) if "Zed" in line)
            terminal.send("m")
            self.wait(terminal, "Desbanir")
            option_row = next(
                i for i, line in enumerate(terminal.screen.display) if "Ver perfil" in line
            )
            self.assertEqual(option_row, min(target_row, terminal.screen.lines - 9) + 3)

    def test_self_profile_first_click_does_not_wait_for_block_prefetch(self):
        with MemberMock() as server:
            ready = threading.Event()
            self.addCleanup(ready.set)
            original = server.api_handler

            def route(method, path, body):
                if method == "GET" and path.endswith("/blocks"):
                    ready.wait(12)
                return original(method, path, body)

            server.api_handler = route
            terminal = self.open(server)
            self.drawer(terminal)
            self.search(terminal, "josé")
            terminal.send("m")
            self.wait(terminal, "Ver perfil")
            self.click(terminal, "Ver perfil")
            self.wait(terminal, "@josé")
            ready.set()

    def test_right_click_selects_target_and_block_option_alternates(self):
        with MemberMock() as server:
            terminal = self.open(server)
            self.drawer(terminal)
            self.menu(terminal, mouse=True)
            self.click(terminal, "Bloquear")
            self.wait(terminal, "Desbloquear")
            self.assertEqual(server.member_calls, [("POST", AUTHOR, "block", None)])
            self.click(terminal, "Desbloquear")
            self.wait(terminal, "Bloquear")
            self.assertEqual(server.member_calls[-1], ("DELETE", AUTHOR, "block", None))
            self.assertEqual(len(server.member_calls), 2)

    def test_right_click_targets_member_under_pointer_without_prior_selection(self):
        with MemberMock() as server:
            terminal = self.open(server)
            self.drawer(terminal)
            self.wait(terminal, "Carol")
            self.right_click(terminal, "Carol")
            self.wait(terminal, "Bloquear")
            self.click(terminal, "Bloquear")
            self.wait(terminal, "Desbloquear")
            self.assertEqual(server.member_calls, [("POST", uuid(5), "block", None)])

    def test_presence_and_role_updates_keep_context_menu_target(self):
        with MemberMock() as server:
            terminal = self.open(server)
            self.drawer(terminal)
            self.menu(terminal)
            server.emit(
                dict(
                    type="presence_update",
                    user_id=AUTHOR,
                    status="busy",
                    nickname="Outro nickname",
                    status_message="Em reunião",
                )
            )
            server.emit(dict(type="role_remove", user_id=AUTHOR, role_id=server.roles[0]["id"]))
            self.wait(terminal, "Outro nickname")
            self.click(terminal, "Bloquear")
            self.wait(terminal, "Desbloquear")
            self.assertEqual(server.member_calls[0][1], AUTHOR)

    def test_refresh_keeps_selected_member(self):
        with MemberMock() as server:
            terminal = self.open(server)
            self.drawer(terminal)
            terminal.send("\x1bOF\x12")
            self.wait(terminal, "Membros (6)")
            terminal.send("m")
            self.wait(terminal, "Desbanir")
            self.assertIn("Zed", self.screen(terminal))

    def test_closing_inflight_reset_keeps_receipt_and_discards_link(self):
        with MemberMock() as server:
            server.member_delay = 0.5
            terminal = self.open(server)
            self.drawer(terminal)
            self.menu(terminal)
            self.click(terminal, "Gerar link de reset")
            self.wait(terminal, "Confirmar")
            terminal.send("\t\n")
            self.settled(terminal, lambda: len(server.member_calls) == 1)
            terminal.send("\x1b[21~")
            self.settled(terminal, lambda: "Membros (" not in self.screen(terminal))
            self.click(terminal, "Conversar em #geral")
            terminal.send("rascunho durante reset")
            self.wait(terminal, "rascunho durante reset")
            terminal.send("\x1b[21~")
            self.wait(terminal, "Membros (1)")
            self.assertNotIn("/passwordchange/", self.screen(terminal))
            self.assertEqual(len(server.member_calls), 1)

    def test_profile_fields_and_no_mutations_when_viewing(self):
        with MemberMock() as server:
            terminal = self.open(server)
            self.drawer(terminal)
            self.search(terminal, "bob")
            terminal.send("\n")
            self.wait(terminal, "@bob")
            self.wait(terminal, "Descrição de perfil sintético.")
            self.wait(terminal, "Frase de status")
            self.wait(terminal, "Cargos")
            self.assertEqual(server.member_calls, [])
            terminal.send("\x1b")
            self.wait(terminal, "Membros (1)")
            terminal.send("m")
            self.wait(terminal, "Bloquear")

    def test_ban_unban_and_reset_require_confirmation(self):
        with MemberMock() as server:
            terminal = self.open(server)
            self.drawer(terminal)
            self.menu(terminal)
            self.click(terminal, "Banir")
            self.wait(terminal, "toda a instância")
            terminal.send("\n")  # Cancel is the default.
            self.wait(terminal, "Gerar link de reset de senha")
            self.assertEqual(server.member_calls, [])
            self.click(terminal, "Banir")
            self.wait(terminal, "Confirmar")
            terminal.send("\x1bOC\n")
            self.wait(terminal, "Desbanir")
            self.assertEqual(server.member_calls[-1], ("PUT", AUTHOR, "ban", {"ban_state": True}))
            self.click(terminal, "Desbanir")
            self.wait(terminal, "Confirmar")
            terminal.send("\t\n")
            self.wait(terminal, "Banir")
            self.assertFalse(server.members[1]["banned"])
            self.click(terminal, "Gerar link de reset")
            self.wait(terminal, "invalida o")
            terminal.send("\t\n")
            self.wait(terminal, "/passwordchange/synthetic-token")
            self.wait(terminal, "2030-01-01")
            self.assertEqual(server.member_calls[-1], ("POST", AUTHOR, "reset", None))
            terminal.send("\x1b")
            self.settled(terminal, lambda: "synthetic-token" not in self.screen(terminal))

    @unittest.skipIf(os.name == "nt", "This case observes the Linux terminal clipboard fallback")
    def test_reset_link_copy_uses_the_exact_url_without_accessing_personal_clipboard(self):
        with MemberMock() as server:
            terminal = self.open(server, environment={"DISPLAY": "", "WAYLAND_DISPLAY": ""})
            self.drawer(terminal)
            self.menu(terminal)
            self.click(terminal, "Gerar link de reset")
            self.wait(terminal, "Confirmar")
            terminal.send("\t\n")
            self.wait(terminal, "/passwordchange/synthetic-token")
            terminal.send("\n")
            self.settled(terminal, lambda: b"\x1b]52;c;" in terminal.output)
            payload = re.search(rb"\x1b]52;c;([^\x07]*)\x07", terminal.output)
            self.assertIsNotNone(payload)
            self.assertEqual(
                base64.b64decode(payload[1]).decode(),
                server.url + "/passwordchange/synthetic-token",
            )

    def test_message_opens_dm_and_focuses_editor(self):
        with MemberMock() as server:
            terminal = self.open(server)
            self.drawer(terminal)
            self.menu(terminal)
            self.click(terminal, "Mensagem")
            self.wait(terminal, "Mensagem direta")
            terminal.send("nova mensagem direta")
            self.wait(terminal, "nova mensagem direta")
            self.assertEqual(server.opens, 1)

    def test_admin_options_follow_manage_server_and_self_has_profile_only(self):
        with MemberMock() as server:
            server.owner_id = uuid(99)
            server.roles[0]["permissions"] = {"ban_members": True}
            server.user_roles = [server.roles[0]]
            terminal = self.open(server)
            self.drawer(terminal)
            self.search(terminal, "bob")
            terminal.send("m")
            self.wait(terminal, "Bloquear")
            self.assertNotIn("Gerar link", self.screen(terminal))
            self.assertNotIn("Banir", self.screen(terminal))
            self.assertNotIn("Desbanir", self.screen(terminal))
            terminal.send("\x1b/\x15josé\n")
            self.settled(
                terminal,
                lambda: "Membros (1)" in self.screen(terminal) and "Você" in self.screen(terminal),
            )
            terminal.send("m")
            self.wait(terminal, "Ver perfil")
            self.assertNotIn("Bloquear", self.screen(terminal))
            self.assertNotIn("Gerar link", self.screen(terminal))

    def test_admin_options_follow_own_role_events_without_refreshing_on_menu_open(self):
        with MemberMock() as server:
            server.owner_id = uuid(99)
            server.roles[0]["permissions"] = {"manage_server": True}
            server.user_roles = [server.roles[0]]
            terminal = self.open(server)
            self.drawer(terminal)
            self.menu(terminal)
            self.wait(terminal, "Banir")
            server.user_roles = []
            server.emit(dict(type="role_remove", user_id=uuid(1), role_id=server.roles[0]["id"]))
            self.settled(terminal, lambda: "Gerar link" not in self.screen(terminal))
            self.assertIn("Bloquear", self.screen(terminal))
            terminal.send("\x1b")
            terminal.read(0.3)
            before = len(server.requests)
            self.right_click(terminal, "Bia")
            self.wait(terminal, "Ver perfil")
            self.assertNotIn("Banir", self.screen(terminal))
            self.assertNotIn("Gerar link", self.screen(terminal))
            self.wait(terminal, "Bloquear")
            terminal.read(0.1)
            self.assertFalse(
                any(
                    path.endswith(("/roles", "/server", "/auth/whoami"))
                    for _, path, *_ in server.requests[before:]
                )
            )
            self.assertNotIn("Banir", self.screen(terminal))
            self.assertNotIn("Gerar link", self.screen(terminal))
            self.assertEqual(server.member_calls, [])

    def test_unknown_permissions_hide_admin_actions_until_successful_retry(self):
        with MemberMock() as server:
            terminal = self.open(server)
            self.drawer(terminal)
            self.menu(terminal)
            terminal.send("\x1b")
            original = server.api_handler

            def route(method, path, body):
                if method == "GET" and path.endswith("/server"):
                    return 503, None
                return original(method, path, body)

            server.api_handler = route
            before = len(server.requests)
            terminal.send("\x12")
            self.settled(
                terminal,
                lambda: any(path.endswith("/server") for _, path, *_ in server.requests[before:]),
            )
            terminal.read(0.3)
            terminal.send("m")
            self.wait(terminal, "Bloquear")
            self.assertNotIn("Banir", self.screen(terminal))
            self.assertNotIn("Gerar link", self.screen(terminal))
            server.api_handler = original
            terminal.send("\x12")
            self.wait(terminal, "Gerar link de reset de senha")

    def test_mouse_and_keyboard_menus_do_not_keep_member_loading_notice(self):
        with MemberMock() as server:
            terminal = self.open(server)
            self.drawer(terminal)
            self.search(terminal, "bob")
            original = server.api_handler

            def route(method, path, body):
                if path.endswith("/user_summary_batch") or path.endswith("/blocks"):
                    time.sleep(0.15)
                return original(method, path, body)

            server.api_handler = route
            for mouse in (True, False):
                if mouse:
                    self.right_click(terminal, "Bia")
                else:
                    terminal.send("m")
                self.wait(terminal, "Ver perfil")
                self.assertNotIn("Carregando membro", self.screen(terminal))
                self.wait(terminal, "Bloquear")
                self.assertNotIn("Carregando membro", self.screen(terminal))
                terminal.send("\x1b")

    def test_members_without_avatar_show_their_first_letter(self):
        with MemberMock() as server:
            terminal = self.open(server)
            self.drawer(terminal)
            self.search(terminal, "bob")
            row = next(i for i, line in enumerate(terminal.screen.display) if "Bia" in line)
            self.assertEqual(terminal.screen.buffer[row][92].data, "B")
            self.assertNotIn("👤", terminal.screen.display[row])

    def dm_fixture(self):
        server = MemberMock()
        second = dm()
        second.update(
            id=uuid(3001), user=dict(id=uuid(5), username="carol", nickname="Carol"), unread_count=0
        )
        server.dms.append(second)
        server.dm_closes = []
        server.dm_close_status = None
        original = server.api_handler

        def route(method, path, body):
            if method == "DELETE" and "/dms/" in path:
                target = path.rsplit("/", 1)[-1]
                server.dm_closes.append(target)
                if server.dm_close_status:
                    return server.dm_close_status, None
                server.dms = [d for d in server.dms if d["id"] != target]
                return 204, None
            return original(method, path, body)

        server.api_handler = route
        return server

    def test_dm_arrow_navigation_works_after_clicking_list_and_opening_dm(self):
        with self.dm_fixture() as server:
            terminal = self.open(server)
            self.click(terminal, "[DMs]")
            self.wait(terminal, "Foco: DMs")
            self.wait(terminal, "Carol ·")
            self.click(terminal, "Bia ·")
            self.wait(terminal, "Mensagem direta")
            self.wait(terminal, "Foco: DMs")
            terminal.read(0.2)
            terminal.send("\x1bOF\x1bOA")
            terminal.send("m\n")
            self.settled(terminal, lambda: server.dm_closes == [uuid(3001)])
            self.wait(terminal, "Mensagem direta")
            self.assertNotIn("Carol ·", self.screen(terminal))
            self.assertEqual(len(server.dms), 1)

    def test_dm_arrows_visibly_move_selection_without_changing_role_colors(self):
        for up, down in (("\x1bOA", "\x1bOB"), ("\x1b[A", "\x1b[B")):
            with self.subTest(arrows=down), self.dm_fixture() as server:
                for conversation in server.dms:
                    conversation["user"]["roles"] = [server.roles[0]]
                terminal = self.open(server)
                self.drawer(terminal)
                terminal.send("\x1b[21~")
                self.click(terminal, "[DMs]")
                self.wait(terminal, "Carol ·")
                terminal.send("\x1bOH")
                terminal.read(0.1)

                def style(name):
                    row = next(
                        i
                        for i, line in enumerate(terminal.screen.display)
                        if name + " ·" in line[:24]
                    )
                    cell = terminal.screen.buffer[row][2]
                    return cell.fg, cell.bg, cell.bold, cell.reverse

                carol, bia = style("Carol"), style("Bia")
                self.assertEqual(carol[0], bia[0])  # Both have the same role.
                self.assertEqual(carol[0], "ff0000")
                self.assertNotEqual(carol[1:], bia[1:], "Selected DM needs a visible highlight")
                before = sum("/messages" in path for _, path, *_ in server.requests)
                terminal.send(down)
                self.settled(terminal, lambda: style("Bia")[1:] == carol[1:])
                self.assertEqual(style("Carol")[1:], bia[1:])
                self.assertEqual(style("Carol")[0], carol[0])
                self.assertEqual(style("Bia")[0], bia[0])
                terminal.send(up)
                self.settled(terminal, lambda: style("Carol")[1:] == carol[1:])
                self.assertEqual(style("Bia")[1:], bia[1:])
                self.assertEqual(
                    sum("/messages" in path for _, path, *_ in server.requests), before
                )
                self.doCleanups()

    def test_member_context_menu_keeps_the_selected_name_visible(self):
        for columns, mouse in ((120, False), (120, True), (60, False), (60, True)):
            with self.subTest(columns=columns, mouse=mouse), MemberMock() as server:
                terminal = self.open(server)
                self.drawer(terminal)
                self.search(terminal, "bob")
                terminal.resize(36, columns)
                self.wait(terminal, "Bia")
                row = next(i for i, line in enumerate(terminal.screen.display) if "Bia" in line)
                column = terminal.screen.display[row].index("Bia")
                if mouse:
                    self.right_click(terminal, "Bia")
                else:
                    terminal.send("m")
                self.wait(terminal, "Gerar link de reset de senha")
                self.assertEqual(terminal.screen.display[row][column : column + 3], "Bia")
                if columns >= 80:
                    menu_row = next(
                        i for i, line in enumerate(terminal.screen.display) if "Ver perfil" in line
                    )
                    menu_left = terminal.screen.display[menu_row].index("Ver perfil") - 2
                    self.assertLess(menu_left + 42, columns - 31)
                self.doCleanups()

    def test_dm_context_menu_keeps_the_selected_recipient_visible(self):
        for columns, mouse in ((120, False), (120, True), (60, False), (60, True)):
            with self.subTest(columns=columns, mouse=mouse), self.dm_fixture() as server:
                terminal = self.open(server)
                self.click(terminal, "[DMs]")
                self.wait(terminal, "Carol ·")
                terminal.resize(36, columns)
                self.wait(terminal, "Carol ·")
                row = next(i for i, line in enumerate(terminal.screen.display) if "Carol ·" in line)
                column = terminal.screen.display[row].index("Carol")
                if mouse:
                    self.right_click(terminal, "Carol ·")
                else:
                    terminal.send("\x1bOHm")
                self.wait(terminal, "Fechar DM")
                self.assertEqual(terminal.screen.display[row][column : column + 5], "Carol")
                self.doCleanups()

    def test_dm_context_menu_targets_pointer_or_selection_without_reading_history(self):
        for mouse in (True, False):
            with self.subTest(mouse=mouse), self.dm_fixture() as server:
                terminal = self.open(server)
                self.click(terminal, "[DMs]")
                self.wait(terminal, "Carol ·")
                before = sum("/messages" in path for _, path, *_ in server.requests)
                if mouse:
                    self.right_click(terminal, "Carol ·")
                else:
                    terminal.send("\x1bOHm")
                self.wait(terminal, "Fechar DM")
                terminal.resize(32, 100)
                self.wait(terminal, "Fechar DM")
                terminal.send("\x1b")
                self.settled(terminal, lambda: "Fechar DM" not in self.screen(terminal))
                self.assertEqual(server.dm_closes, [])
                terminal.send("m")
                self.wait(terminal, "Fechar DM")
                if mouse:
                    self.click(terminal, "Fechar DM")
                else:
                    terminal.send("\n")
                self.settled(terminal, lambda: server.dm_closes == [uuid(3001)])
                self.settled(terminal, lambda: "Carol ·" not in self.screen(terminal))
                self.assertEqual(
                    sum("/messages" in path for _, path, *_ in server.requests), before
                )
                self.assertEqual(server.dms[0]["user"]["nickname"], "Bia")
                self.doCleanups()

    def test_failed_dm_close_keeps_conversation_and_is_not_retried(self):
        for status in (403, 429, 503):
            with self.subTest(status=status), self.dm_fixture() as server:
                server.dm_close_status = status
                terminal = self.open(server)
                self.click(terminal, "[DMs]")
                self.wait(terminal, "Carol ·")
                self.right_click(terminal, "Carol ·")
                self.wait(terminal, "Fechar DM")
                terminal.send("\n")
                self.settled(terminal, lambda: len(server.dm_closes) == 1)
                terminal.read(0.3)
                self.assertIn("Carol ·", self.screen(terminal))
                self.assertEqual(len(server.dm_closes), 1)
                self.doCleanups()

    def test_failed_actions_are_never_replayed(self):
        for status in (403, 404, 429, 503):
            with self.subTest(status=status), MemberMock() as server:
                server.member_status = status
                terminal = self.open(server)
                self.drawer(terminal)
                self.menu(terminal)
                self.click(terminal, "Bloquear")
                self.wait(terminal, "Resultado incerto" if status == 503 else "Ação recusada")
                terminal.send("\n\n")
                self.assertEqual(len(server.member_calls), 1)
                self.doCleanups()

    def test_malformed_reset_response_is_uncertain_and_never_replayed(self):
        with MemberMock() as server:
            server.reset_response = {}
            terminal = self.open(server)
            self.drawer(terminal)
            self.menu(terminal)
            self.click(terminal, "Gerar link de reset")
            self.wait(terminal, "Confirmar")
            terminal.send("\t\n")
            self.wait(terminal, "Resultado incerto")
            terminal.send("\x12")
            self.wait(terminal, "Gerar link de reset de senha")
            self.assertEqual(len(server.member_calls), 1)
            self.assertNotIn("/passwordchange/", self.screen(terminal))

    def test_notification_and_settings_modals_own_input_and_cursor_above_drawer(self):
        with MemberMock() as server:
            terminal = self.open(server)
            self.drawer(terminal)
            terminal.send("/")
            self.settled(terminal, lambda: not terminal.screen.cursor.hidden)
            terminal.send("\x1b[20~")
            self.wait(terminal, "Notificações")
            self.settled(terminal, lambda: terminal.screen.cursor.hidden)
            terminal.send("\t\t\tfiltro exclusivo")
            self.wait(terminal, "filtro exclusivo")
            row = next(
                i for i, line in enumerate(terminal.screen.display) if "filtro exclusivo" in line
            )
            self.settled(terminal, lambda: terminal.screen.cursor.y == row)
            terminal.send("\x1b")
            self.wait(terminal, "Membros (6)")
            self.assertNotIn("filtro exclusivo", self.screen(terminal))
            terminal.send("\x1bOQ")
            self.wait(terminal, "Configurações")
            self.settled(terminal, lambda: terminal.screen.cursor.hidden)
            terminal.send("\x1b")
            self.settled(terminal, lambda: not terminal.screen.cursor.hidden)

    def test_profile_and_block_read_failures_can_be_retried(self):
        with MemberMock() as server:
            server.blocks_status = 503
            terminal = self.open(server)
            self.drawer(terminal)
            self.search(terminal, "bob")
            terminal.send("m")
            self.wait(terminal, "Falha ao consultar membro")
            self.assertNotIn("Desbloquear", self.screen(terminal))
            server.blocks_status = None
            terminal.send("\x12")
            self.wait(terminal, "Bloquear")
            server.profile_status = 404
            self.click(terminal, "Ver perfil")
            self.wait(terminal, "Membro não encontrado")
            server.profile_status = None
            terminal.send("\x12")
            self.wait(terminal, "@bob")

    def test_pagination_searches_members_after_first_page(self):
        with MemberMock(total=205) as server:
            terminal = self.open(server)
            self.drawer(terminal)
            self.wait(terminal, "Membros (205)")
            self.search(terminal, "user0204")
            self.wait(terminal, "user0204")
            self.assertEqual(
                sum("/users?" in path for method, path, *_ in server.requests if method == "GET"), 3
            )


if __name__ == "__main__":
    unittest.main()
