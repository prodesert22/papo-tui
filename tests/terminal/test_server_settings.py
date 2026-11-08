"""Configurações do servidor through the real TUI and synthetic REST."""

from test_context import main as test_main

import base64
import tempfile
import time
import unittest
from pathlib import Path

from history_mock import OTHER, uuid
from server_admin_mock import EMOJI, ME, MODERATOR, OTHER_OWNER, PIXEL, ServerAdminMock
from terminal_support import Terminal
import terminal_interactions as interactions

UP, DOWN, RIGHT, LEFT = "\x1bOA", "\x1bOB", "\x1bOC", "\x1bOD"
TITLE = "Configurações do servidor ·"
TABS = ("Visão geral", "Cargos", "Canais", "Emojis", "Membros", "Auditoria")


class ServerSettings(unittest.TestCase):
    screen = interactions.MessageInteractions.screen
    settled = interactions.MessageInteractions.settled
    click = interactions.MessageInteractions.click

    def wait(self, terminal, text, seconds=8):
        self.settled(terminal, lambda: text in self.screen(terminal), seconds=seconds)

    def login(self, server):
        """Signs in and switches to Ultralight."""
        t = Terminal()
        self.addCleanup(t.close)
        t.resize(30, 100)
        t.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(t, "Conversar em #geral")
        t.send("\x13")
        self.wait(t, "Seu perfil em")
        t.send(RIGHT + "\t" + DOWN * 2 + "\n\t\n")
        self.settled(t, lambda: "Seu perfil em" not in self.screen(t))
        self.wait(t, "Conversar em #geral")
        return t

    def open(self, server):
        """Signs in and opens the server's settings from the server tab menu."""
        t = self.login(server)
        deadline = time.monotonic() + 8
        while "Foco: Servidores" not in self.screen(t) and time.monotonic() < deadline:
            t.send(UP)
            t.read(0.1)
        self.wait(t, "Foco: Servidores")
        t.send("m")
        self.wait(t, "Configurações do servidor")
        t.send("\n")
        self.wait(t, TITLE)
        return t

    def tabs(self, terminal):
        row = next(line for line in terminal.screen.display if "<" in line and ">" in line)
        return [tab for tab in TABS if tab in row]

    def tab(self, terminal, name):
        self.click(terminal, name)
        self.wait(terminal, f"< {name} >")

    def calls(self, server, method, prefix, count, seconds=6):
        def found():
            return [c for c in server.admin_calls if c[0] == method and c[1].startswith(prefix)]

        deadline = time.monotonic() + seconds
        while len(found()) < count and time.monotonic() < deadline:
            time.sleep(0.03)
        self.assertEqual(len(found()), count, server.admin_calls)
        return found()

    def type_in(self, terminal, label, text):
        """Focuses the text field after label, clears it and types text."""
        self.click(terminal, label + ":")
        terminal.send("\x03" + text)
        self.wait(terminal, text)

    def test_owner_sees_every_tab_and_renames_the_server(self):
        with ServerAdminMock() as server:
            t = self.open(server)
            self.wait(t, "Comunidade")
            self.wait(t, "josé")
            self.assertEqual(self.tabs(t), list(TABS))
            self.type_in(t, "Nome", "Clube do Papo")
            t.send("\n")
            self.wait(t, "Servidor atualizado")
            ((_, _, body),) = self.calls(server, "PATCH", "/server", 1)
            self.assertEqual(body, {"name": "Clube do Papo"})
            # Over 32 characters is refused locally, before any request.
            self.type_in(t, "Nome", "x" * 33)
            t.send("\n")
            self.wait(t, "Nome com mais de 32 caracteres.")
            self.calls(server, "PATCH", "/server", 1)

    def test_making_the_server_private_requires_a_password_that_is_never_shown(self):
        with ServerAdminMock() as server:
            t = self.open(server)
            self.click(t, "Público >")
            self.wait(t, "< Privado >")
            self.click(t, "Salvar alterações")
            self.wait(t, "Informe a senha do servidor para torná-lo privado.")
            self.type_in_secret(t, "Senha", "Segredo!42")
            self.click(t, "Salvar alterações")
            self.wait(t, "Servidor atualizado")
            ((_, _, body),) = self.calls(server, "PATCH", "/server", 1)
            self.assertEqual(body, {"public": False, "password": "Segredo!42"})
            self.assertNotIn(b"Segredo!42", t.output)
            # Back on the server copy, private servers offer an optional new password.
            self.wait(t, "Nova senha:")

    def type_in_secret(self, terminal, label, text):
        self.click(terminal, label + ":")
        terminal.send(text)
        self.settled(terminal, lambda: "*" * len(text) in self.screen(terminal))

    def test_tabs_follow_the_account_permissions(self):
        with ServerAdminMock() as server:
            server.set_owner(OTHER_OWNER)
            t = self.open(server)
            # Adding emojis needs no permission; every other tab is hidden.
            self.wait(t, "< Emojis >")
            self.assertEqual(self.tabs(t), ["Emojis"])
            t.send("\x1b")
            self.settled(t, lambda: TITLE not in self.screen(t))
        with ServerAdminMock() as server:
            server.set_owner(OTHER_OWNER)
            server.give("manage_roles")
            t = self.open(server)
            self.wait(t, "< Cargos >")
            self.assertEqual(self.tabs(t), ["Cargos", "Emojis", "Membros"])

    def test_a_tab_closes_when_its_permission_is_removed(self):
        with ServerAdminMock() as server:
            server.set_owner(OTHER_OWNER)
            server.give("manage_channels")
            t = self.open(server)
            self.wait(t, "< Canais >")
            self.wait(t, "# geral")
            server.user_roles = []
            server.me()["roles"] = []
            server.emit({"type": "role_remove", "user_id": ME, "role_id": MODERATOR})
            self.wait(t, "Permissão removida; a aba foi fechada.")
            self.wait(t, "< Emojis >")
            self.assertEqual(self.tabs(t), ["Emojis"])

    def test_roles_are_created_edited_and_deleted(self):
        with ServerAdminMock() as server:
            t = self.open(server)
            self.tab(t, "Cargos")
            self.wait(t, "+ Novo cargo")
            self.type_in(t, "Nome", "Suporte")
            self.type_in(t, "Cor", "#12ab34")
            self.click(t, "[ ] Fixar mensagens")
            self.wait(t, "[x] Fixar mensagens")
            self.click(t, "Criar cargo")
            self.wait(t, "Cargo criado")
            ((_, _, body),) = self.calls(server, "POST", "/roles", 1)
            self.assertEqual(body["name"], "Suporte")
            self.assertEqual(body["color"], "#12ab34")
            self.assertTrue(body["permissions"]["pin_message"])
            self.assertFalse(body["permissions"]["manage_server"])
            self.wait(t, "Suporte")
            # An invalid color never leaves the client.
            self.click(t, "Equipe")
            self.wait(t, "Editar cargo")
            self.type_in(t, "Cor", "vermelho")
            self.click(t, "Salvar cargo")
            self.wait(t, "Use uma cor no formato #RRGGBB")
            self.calls(server, "PUT", "/roles/", 0)
            self.type_in(t, "Nome", "Equipe técnica")
            self.type_in(t, "Cor", "#00FF00")
            self.click(t, "Salvar cargo")
            self.wait(t, "Cargo salvo")
            ((_, path, body),) = self.calls(server, "PUT", "/roles/", 1)
            self.assertEqual(body["name"], "Equipe técnica")
            # Deleting asks for a second press.
            self.click(t, "Equipe técnica")
            self.click(t, "Excluir cargo")
            self.wait(t, "Confirmar: Excluir cargo")
            self.calls(server, "DELETE", "/roles/", 0)
            self.click(t, "Confirmar: Excluir cargo")
            self.wait(t, "Cargo excluído")
            ((_, deleted, _),) = self.calls(server, "DELETE", "/roles/", 1)
            self.assertEqual(deleted, path)
            self.settled(t, lambda: "Equipe técnica" not in self.screen(t))

    def test_refused_and_unconfirmed_changes_are_never_repeated(self):
        with ServerAdminMock() as server:
            server.admin_status[("POST", "/roles")] = 409
            t = self.open(server)
            self.tab(t, "Cargos")
            self.type_in(t, "Nome", "Equipe")
            self.click(t, "Criar cargo")
            self.wait(t, "Já existe um cargo com esse nome")
            # The draft stays for a correction; remote text is sanitized.
            self.wait(t, "Nome:             Equipe")
            self.assertNotIn(b"\x1b]52;", t.output)
            server.admin_status[("POST", "/roles")] = "drop"
            self.type_in(t, "Nome", "Outro")
            self.click(t, "Criar cargo")
            self.wait(t, "Alteração não confirmada; confira antes de repetir")
            time.sleep(1)
            self.calls(server, "POST", "/roles", 2)

    def right_click(self, terminal, text):
        self.wait(terminal, text)
        row = next(i for i, line in enumerate(terminal.screen.display) if text in line)
        column = terminal.screen.display[row].index(text) + 1
        terminal.send(f"\x1b[<2;{column + 1};{row + 1}M\x1b[<2;{column + 1};{row + 1}m")

    def test_channel_menu_opens_the_channel_in_server_settings(self):
        with ServerAdminMock() as server:
            t = self.login(server)
            self.right_click(t, "# outro")
            self.wait(t, "Configurar canal…")
            self.click(t, "Configurar canal…")
            self.wait(t, TITLE)
            self.wait(t, "< Canais >")
            self.wait(t, "Editar canal")
            self.wait(t, "Nome:             outro")
            # The form has the focus: typing edits the channel's name.
            t.send("\x03avisos\n")
            self.wait(t, "Canal salvo")
            ((_, path, body),) = self.calls(server, "PUT", "/channels/", 1)
            self.assertEqual((path, body["name"]), (f"/channels/{OTHER}", "avisos"))
            # Voice channels list Abrir chat first; the option stays last.
            t.send("\x1b")
            self.settled(t, lambda: TITLE not in self.screen(t))
            self.right_click(t, "♪ voz")
            self.wait(t, "Configurar canal…")
            menu = [
                line for line in t.screen.display if "Abrir chat" in line or "Configurar" in line
            ]
            self.assertEqual(len(menu), 2)
            self.assertLess(
                self.screen(t).index("Abrir chat"), self.screen(t).index("Configurar canal…")
            )

    def test_channel_menu_offers_configuration_only_with_manage_channels(self):
        with ServerAdminMock() as server:
            server.set_owner(OTHER_OWNER)
            t = self.login(server)
            self.right_click(t, "# outro")
            self.wait(t, "Notificações…")
            t.read(0.3)
            self.assertNotIn("Configurar canal", self.screen(t))

    def test_channels_are_created_reordered_and_restricted_by_role(self):
        with ServerAdminMock() as server:
            t = self.open(server)
            self.tab(t, "Canais")
            self.wait(t, "── Categoria")
            self.type_in(t, "Nome", "avisos")
            self.click(t, "Texto >")
            self.wait(t, "< Voz >")
            self.type_in(t, "Tópico", "Só avisos")
            self.click(t, "Criar canal")
            self.wait(t, "Canal criado")
            ((_, _, body),) = self.calls(server, "POST", "/channels", 1)
            self.assertEqual(body, {"name": "avisos", "type": "voice", "topic": "Só avisos"})
            self.click(t, "# outro")
            self.wait(t, "Editar canal")
            self.click(t, "Mover para cima")
            self.wait(t, "Canal movido")
            ((_, path, body),) = self.calls(server, "PUT", f"/channels/{OTHER}/change_position", 1)
            self.assertEqual(body, {"old_position": 3, "new_position": 2})
            # A stale position is refused by the server and never retried.
            server.admin_status[("PUT", f"/channels/{OTHER}/change_position")] = 409
            self.click(t, "Mover para cima")
            self.wait(t, "A ordem dos canais mudou")
            self.calls(server, "PUT", f"/channels/{OTHER}/change_position", 2)
            self.click(t, "Vincular cargo")
            self.wait(t, "Permissões do canal salvas")
            ((_, path, body),) = self.calls(server, "PUT", f"/channels/{OTHER}/permissions/", 1)
            role = path.rsplit("/", 1)[-1]
            self.assertEqual(
                body["permissions"],
                {
                    "read_channel": True,
                    "send_messages": True,
                    "delete_messages": False,
                    "connect_voice": False,
                },
            )
            self.wait(t, "[x] Enviar mensagens")
            self.click(t, "[x] Enviar mensagens")
            self.wait(t, "[ ] Enviar mensagens")
            (_, _, body) = self.calls(server, "PUT", f"/channels/{OTHER}/permissions/", 2)[1]
            self.assertFalse(body["permissions"]["send_messages"])
            self.click(t, "Remover vínculo")
            self.click(t, "Confirmar: Remover vínculo")
            self.wait(t, "Vínculo do cargo removido")
            self.calls(server, "DELETE", f"/channels/{OTHER}/role/{role}", 1)

    def test_emojis_are_added_from_a_file_and_deleted(self):
        with tempfile.TemporaryDirectory() as folder, ServerAdminMock() as server:
            image = Path(folder, "aceno.png")
            image.write_bytes(base64.b64decode(PIXEL))
            server.set_owner(OTHER_OWNER)
            t = self.open(server)
            self.wait(t, ":festa:")
            # Someone else's emoji without manage_server cannot be deleted.
            self.click(t, ":festa:")
            self.wait(t, "Só quem adicionou o emoji")
            # Ultralight shows no picture: the details follow the name directly.
            self.assertNotIn("Carregando imagem", self.screen(t))
            row = next(i for i, line in enumerate(t.screen.display) if "Formato:" in line)
            self.assertIn(":festa:", t.screen.display[row - 1])
            self.assertNotIn("Excluir emoji", self.screen(t))
            self.click(t, "+ Adicionar emoji")
            self.click(t, "Escolher imagem e enviar")
            self.wait(t, "Preencha este campo.")
            self.type_in(t, "Nome", "aceno")
            self.click(t, "Escolher imagem e enviar")
            self.wait(t, "Escolher imagem")
            t.send("\x03" + str(image) + "\n")
            self.wait(t, "Emoji adicionado")
            ((_, _, body),) = self.calls(server, "POST", "/emojis", 1)
            self.assertEqual((body["name"], body["format"]), ("aceno", "PNG"))
            self.wait(t, ":aceno:")
            self.click(t, ":aceno:")
            self.click(t, "Excluir emoji")
            self.click(t, "Confirmar: Excluir emoji")
            self.wait(t, "Emoji excluído")
            self.calls(server, "DELETE", "/emojis/", 1)
            self.settled(t, lambda: ":aceno:" not in self.screen(t))
            self.assertNotIn(f"/emojis/{EMOJI}", [c[1] for c in server.admin_calls])

    def test_members_get_roles_and_are_banned(self):
        with ServerAdminMock() as server:
            t = self.open(server)
            self.tab(t, "Membros")
            self.wait(t, "@alice")
            t.send("\t")  # From the tabs to the list: typing filters it.
            t.send("car")
            self.wait(t, "Buscar: car")
            self.settled(t, lambda: "@alice" not in self.screen(t))
            self.click(t, "Carol")
            self.wait(t, "[x] Equipe")
            self.click(t, "[ ] Comunidade")
            self.wait(t, "Cargo atribuído")
            ((_, path, body),) = self.calls(server, "POST", f"/users/{uuid(5)}/roles", 1)
            self.assertEqual(body["role_id"], server.roles[1]["id"])
            self.wait(t, "[x] Comunidade")
            self.click(t, "[x] Equipe")
            self.wait(t, "Cargo removido do membro")
            self.calls(server, "DELETE", f"/users/{uuid(5)}/roles/", 1)
            self.click(t, "Banir")
            self.click(t, "Confirmar: Banir")
            self.wait(t, "Membro banido")
            self.assertEqual(server.member_calls[-1][1:3], (uuid(5), "ban"))
            self.wait(t, "Desbanir")
            self.click(t, "Gerar link de redefinição de senha")
            self.click(t, "Confirmar: Gerar link")
            self.wait(t, "passwordchange/synthetic-")

    def audit(self, server):
        t = self.open(server)
        self.tab(t, "Auditoria")
        self.wait(t, "▾ Filtros")
        self.wait(t, "role.create")
        self.wait(t, "Aplicar filtros")
        return t

    def audit_reads(self, server):
        return [c[1] for c in server.admin_calls if c[0] == "GET" and "audit-logs" in c[1]]

    def audit_read(self, server, expected, seconds=6):
        """The label changes before the request reaches the server: wait for it."""
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline and not any(
            expected(path) if callable(expected) else path == expected
            for path in self.audit_reads(server)
        ):
            time.sleep(0.03)
        self.assertTrue(
            any(
                expected(path) if callable(expected) else path == expected
                for path in self.audit_reads(server)
            ),
            self.audit_reads(server),
        )

    def test_the_audit_log_pages_and_shows_event_details(self):
        with ServerAdminMock() as server:
            t = self.audit(server)
            self.audit_read(server, "/papo/admin/audit-logs?order=desc")
            t.send("\t")  # The list; Page Down reaches its last row.
            for _ in range(12):
                t.send("\x1b[6~")
                t.read(0.05)
            self.wait(t, "Carregar mais…")
            t.send("\n")
            page = self.calls(server, "GET", "/papo/admin/audit-logs?order=desc&since=", 1)
            self.assertIn("last_id=", page[0][1])
            # Remote metadata is shown as text; its control sequence is not.
            self.click(t, "user.ban")
            self.wait(t, "Membro afetado")
            self.wait(t, '"note":')
            self.assertNotIn(b"\x1b]52;", t.output)

    def test_audit_filters_by_entity_action_and_author(self):
        with ServerAdminMock() as server:
            t = self.audit(server)
            self.click(t, "Todas >")  # Entidade, the first option.
            self.wait(t, "< Usuários >")
            self.click(t, "Aplicar filtros")
            self.wait(t, "▾ Filtros (1 ativo)")
            self.audit_read(server, "/papo/admin/audit-logs?order=desc&entity_type=user")
            self.settled(t, lambda: "role.create" not in self.screen(t))
            self.click(t, "Limpar filtros")
            self.wait(t, "role.create")
            # Actions seen in the log can be chosen; ← from Todas wraps to the last.
            self.click(t, "Ação:")
            self.wait(t, "< user.ban >")
            # The author is found by name in the member directory.
            self.type_in(t, "Feito por", "ali")
            self.click(t, "Álice (@alice)")
            self.wait(t, "Qualquer pessoa")
            self.click(t, "Aplicar filtros")
            self.wait(t, "▾ Filtros (2 ativos)")
            self.audit_read(
                server, f"/papo/admin/audit-logs?order=desc&action=user.ban&actor_id={uuid(4)}"
            )
            self.settled(t, lambda: "josé" not in self.screen(t).split("Filtros")[-1])
            # An event narrows the filters to its action or its author.
            self.click(t, "Limpar filtros")
            self.wait(t, "role.create")
            self.click(t, "role.create")
            self.wait(t, "Só as ações de")
            self.click(t, "Só a ação role.create")
            self.wait(t, "▾ Filtros (1 ativo)")
            self.audit_read(server, "/papo/admin/audit-logs?order=desc&action=role.create")

    def test_audit_filters_by_date(self):
        with ServerAdminMock() as server:
            t = self.audit(server)
            self.type_in(t, "De", "31/02/2026")
            self.click(t, "Aplicar filtros")
            self.wait(t, "Data inválida")
            reads = len(self.audit_reads(server))
            self.type_in(t, "De", "12/10/2026")
            self.type_in(t, "Até", "11/10/2026")
            self.click(t, "Aplicar filtros")
            self.wait(t, "A data inicial é depois da final.")
            self.assertEqual(len(self.audit_reads(server)), reads)
            self.type_in(t, "De", "11/10/2026")
            self.click(t, "Aplicar filtros")
            self.wait(t, "▾ Filtros (2 ativos)")
            # "Até" goes to the server; "De" ends the pages at older events.
            self.audit_read(
                server, "/papo/admin/audit-logs?order=desc&until=2026-10-11T23%3A59%3A59Z"
            )
            t.send("\t")
            for _ in range(6):
                t.send("\x1b[6~")
                t.read(0.05)
            t.read(0.5)
            self.assertNotIn("Carregar mais…", self.screen(t))
            self.assertEqual(len(self.audit_reads(server)), reads + 1)
            # The form shows the dates in use after applying them.
            t.send("\x1b[5~" * 6)
            self.wait(t, "De:               11/10/2026")


if __name__ == "__main__":
    test_main(1)
