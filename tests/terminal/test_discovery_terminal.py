"""Pinned/search UI against synthetic HTTP/WS using a local terminal transport."""

import time
import os
from pathlib import Path
import tempfile
import unittest
from urllib.parse import parse_qs, urlsplit
from discovery_mock import DiscoveryMock
from history_mock import CHANNEL, uuid
import test_r8_terminal as r8


class DiscoveryTerminal(unittest.TestCase):
    login = r8.R8Terminal.login

    def until(self, t, predicate):
        end = time.monotonic() + 7
        while not predicate() and time.monotonic() < end:
            t.read(0.03)
        self.assertTrue(predicate(), "\n".join(t.screen.display))

    def wait(self, t, text):
        r8.R8Terminal.wait(self, t, text)
        if text in ("Buscar mensagens no servidor", "Mensagens fixadas"):
            height, width = min(t.screen.lines - 2, 25), min(t.screen.columns - 4, 90)
            row = (t.screen.lines - height) // 2 + height - 1
            column = (t.screen.columns - width) // 2 + width - 1
            self.until(t, lambda: t.screen.buffer[row][column].data in ("j", "┘"))

    def resize(self, t, rows, columns):
        t.resize(rows, columns)
        self.until(t, lambda: t.screen.buffer[rows - 1][columns - 1].data in ("j", "┘"))

    def assert_centered_target(self, t, content):
        self.until(t, lambda: "Localizando mensagem" not in "\n".join(t.screen.display))
        r8.R8Terminal.assert_centered_target(self, t, content)

    @unittest.skipIf(os.name == "nt", "Native Windows file dialogs cannot be driven through ConPTY")
    def test_integrated_search_and_file_picker_shortcuts_do_not_collide(self):
        with DiscoveryMock(10) as server, tempfile.TemporaryDirectory() as folder:
            source = Path(folder, "integration.txt")
            source.write_text("Integrated attachment")
            t = self.login(server)
            t.send("\x1bOR")
            self.wait(t, "Buscar mensagens no servidor")
            t.send("\x1b")
            t.send("\x1b[15~")
            self.wait(t, "Anexar arquivos")
            t.send("\x03" + str(source) + "\n")
            self.wait(t, "integration.txt 🗙]")
            t.send("Arquivo integrado\n")
            self.wait(t, "Mensagem confirmada")
            self.assertEqual(server.uploads[0][0]["data"], b"Integrated attachment")
            self.assertEqual(server.sent_messages[0]["content"], "Arquivo integrado")

    def test_integrated_pin_permission_is_independent_of_delete_permission(self):
        with DiscoveryMock(10) as server:
            role = uuid(401)
            server.owner_id = uuid(99)
            server.roles = [dict(id=role, name="Pins", permissions=dict(pin_message=True))]
            server.user_roles = [dict(id=role)]
            t = self.login(server)
            self.resize(t, 32, 160)
            t.send("\x1b[18~m")
            self.wait(t, "Fixar mensagem")
            self.assertNotIn("Excluir mensagem", "\n".join(t.screen.display))
            self.click(t, "Fixar mensagem")
            self.wait(t, "Mensagem fixada.")
            self.assertEqual(len(server.pin_calls), 1)
            server.user_roles = []
            server.emit(dict(type="role_remove", user_id=uuid(1), role_id=role))
            t.read(0.3)
            t.send("m")
            self.wait(t, "Ver quem reagiu")
            self.assertNotIn("Desafixar mensagem", "\n".join(t.screen.display))
            t.send("\x1b\x1b[19~")
            self.wait(t, "4 resultado(s)")
            t.send("0010\n")
            self.wait(t, "Detalhes da mensagem")
            t.send("\x1b[C\n")
            self.wait(t, "Sem permissão para desafixar")
            self.assertEqual(len(server.pin_calls), 1)

    @unittest.skipIf(os.name == "nt", "Native Windows file dialogs cannot be driven through ConPTY")
    def test_integrated_attachment_draft_cannot_send_after_role_revocation(self):
        with DiscoveryMock(10) as server, tempfile.TemporaryDirectory() as folder:
            role = uuid(401)
            server.owner_id = uuid(99)
            server.roles = [dict(id=role, name="Files", permissions=dict(send_attachment=True))]
            server.user_roles = [dict(id=role)]
            source = Path(folder, "revoked.txt")
            source.write_text("Permission gated")
            t = self.login(server)
            self.resize(t, 32, 160)
            t.send("\x1b[15~")
            self.wait(t, "Anexar arquivos")
            t.send("\x03" + str(source) + "\n")
            self.wait(t, "revoked.txt 🗙]")
            server.user_roles = []
            server.emit(dict(type="role_remove", user_id=uuid(1), role_id=role))
            t.read(0.3)
            t.send("\n")
            self.wait(t, "Sem permissão")
            self.assertEqual(server.message_count, 0)
            self.assertIn("revoked.txt 🗙]", "\n".join(t.screen.display))
            t.send("\x1b[15~")
            self.wait(t, "Permissão indisponível para anexar arquivos.")
            self.assertNotIn("Anexar arquivos", "\n".join(t.screen.display))

    def click(self, t, label):
        self.wait(t, label)
        t.read(0.08)
        row = next(i for i, line in enumerate(t.screen.display) if label in line)
        t.click(row, t.screen.display[row].index(label) + 1)

    def button_style(self, t, label):
        row = next(i for i, line in enumerate(t.screen.display) if label in line)
        cell = t.screen.buffer[row][t.screen.display[row].index(label)]
        return cell.fg, cell.bg, cell.reverse

    def history_reads(self, server):
        return [
            parse_qs(urlsplit(path).query)
            for method, path, _, _ in server.requests
            if method == "GET" and urlsplit(path).path.endswith(CHANNEL + "/messages")
        ]

    def assert_location_animates(self, t):
        self.wait(t, "Localizando mensagem…")
        frames = set()
        for _ in range(3):
            line = next(line for line in t.screen.display if "Localizando mensagem…" in line)
            frames.add(line)
            self.assertNotIn("Localizando", t.screen.display[-2])
            t.read(0.13)
        self.assertGreater(len(frames), 1)

    def test_ancient_pin_and_search_jump_uses_two_reads_and_keeps_animated_modal(self):
        for pins in (True, False):
            with self.subTest(pins=pins), DiscoveryMock(10005) as server:
                target = server.messages[39]
                target["content"] = "Destino muito antigo"
                server.pin_ids[CHANNEL] = [target["id"]]
                t = self.login(server)
                self.resize(t, 32, 100)
                self.wait(t, "Mensagem 10005")
                t.send("\x1b[19~" if pins else "\x1bOR")
                self.wait(t, "Mensagens fixadas" if pins else "Buscar mensagens no servidor")
                t.send("Destino muito antigo\n")
                if not pins:
                    self.wait(t, "1 resultado(s)")
                    t.send("\n")
                self.wait(t, "Detalhes da mensagem")
                before = len(self.history_reads(server))
                server.delay = 0.8
                t.send("\n\n")  # Repeated Enter must not enqueue another jump.
                self.assert_location_animates(t)
                self.resize(t, 22, 44)
                self.wait(t, "Localizando mensagem…")
                self.resize(t, 32, 100)
                self.assert_centered_target(t, target["content"])
                reads = self.history_reads(server)[before:]
                self.assertEqual(len(reads), 2)
                self.assertEqual(reads[0]["order"], ["asc"])
                self.assertEqual(reads[0]["last_id"], [target["id"]])
                self.assertEqual(reads[1]["order"], ["desc"])
                self.assertNotIn("Localizando mensagem", "\n".join(t.screen.display))

    def test_cancel_location_ignores_late_reads_and_preserves_details(self):
        for phase in ("asc", "desc"):
            with self.subTest(phase=phase), DiscoveryMock(205) as server:
                target = server.messages[39]
                server.pin_ids[CHANNEL] = [target["id"]]
                t = self.login(server)
                self.resize(t, 32, 100)
                self.wait(t, "Mensagem 0205")
                t.send("\x1b[19~")
                self.wait(t, "1 resultado(s)")
                t.send("0040\n")
                self.wait(t, "Detalhes da mensagem")
                before = len(self.history_reads(server))
                server.delay = 0.7
                t.send("\n")
                self.wait(t, "Localizando mensagem…")
                deadline = time.monotonic() + 4
                while time.monotonic() < deadline:
                    reads = self.history_reads(server)[before:]
                    if reads and reads[-1]["order"] == [phase]:
                        break
                    t.read(0.03)
                self.assertEqual(reads[-1]["order"], [phase])
                t.send("\x1b")
                self.wait(t, "Detalhes da mensagem")
                t.read(1)
                self.wait(t, "Detalhes da mensagem")
                self.assertEqual(
                    len(self.history_reads(server)) - before, 1 if phase == "asc" else 2
                )
                t.send("\x1b")
                self.wait(t, "1 resultado(s)")
                self.assertIn("0040", "\n".join(t.screen.display))
                t.send("\x1b")
                self.wait(t, "Mensagem 0205")

    def test_location_error_and_missing_target_stay_in_modal_with_retry(self):
        with DiscoveryMock(205) as server:
            target = server.messages[39]
            server.pin_ids[CHANNEL] = [target["id"]]
            t = self.login(server)
            self.resize(t, 32, 100)
            self.wait(t, "Mensagem 0205")
            t.send("\x1b[19~")
            self.wait(t, "1 resultado(s)")
            t.send("0040\n")
            self.wait(t, "Detalhes da mensagem")
            server.recovery_status = 403
            t.send("\n")
            self.wait(t, "Falha ao localizar mensagem")
            self.assertNotIn("Falha ao localizar", t.screen.display[-2])
            before = len(self.history_reads(server))
            t.read(0.4)
            self.assertEqual(len(self.history_reads(server)), before)
            server.recovery_status = None
            server.messages.remove(target)
            t.send("\n")
            self.wait(t, "Mensagem não disponível.")
            self.assertNotIn("Mensagem não disponível", t.screen.display[-2])
            t.send("\x1b")
            self.wait(t, "Detalhes da mensagem")

    def test_notification_also_keeps_animated_location_modal_until_ready(self):
        with DiscoveryMock(320) as server:
            target = server.messages[39]
            server.notifications[0].update(
                message_id=target["id"], channel_id=CHANNEL, message_content="Destino de aviso"
            )
            t = self.login(server)
            self.resize(t, 32, 100)
            self.wait(t, "Mensagem 0320")
            t.send("\x1b[20~")
            self.wait(t, "Destino de aviso")
            t.send("\n")
            self.wait(t, "Detalhes da notificação")
            server.delay = 0.8
            self.click(t, "[Abrir conversa]")
            self.assert_location_animates(t)
            self.assert_centered_target(t, target["content"])

    def test_cancel_cross_channel_search_preserves_preview_results_and_can_retry(self):
        with DiscoveryMock(205) as server:
            target = server.messages[39]
            t = self.login(server)
            self.resize(t, 32, 100)
            self.click(t, "outro")
            self.wait(t, "Conteúdo do outro canal")
            t.send("\x1bOR")
            self.wait(t, "Buscar mensagens no servidor")
            t.send("Mensagem 0040\n")
            self.wait(t, "1 resultado(s)")
            t.send("\n")
            self.wait(t, "Detalhes da mensagem")
            server.delay = 0.7
            t.send("\n")
            self.wait(t, "Localizando mensagem…")
            t.send("\x1b")
            self.wait(t, "Detalhes da mensagem")
            self.wait(t, "Mensagem 0040")
            t.send("\x1b")
            self.wait(t, "1 resultado(s)")
            t.send("\n\n")
            self.assert_centered_target(t, target["content"])

    def test_reconnect_does_not_leave_location_spinning_forever(self):
        with DiscoveryMock(205) as server:
            server.pin_ids[CHANNEL] = [server.messages[39]["id"]]
            t = self.login(server)
            self.resize(t, 32, 100)
            self.wait(t, "Mensagem 0205")
            t.send("\x1b[19~")
            self.wait(t, "1 resultado(s)")
            t.send("\n")
            self.wait(t, "Detalhes da mensagem")
            server.delay = 1
            t.send("\n")
            self.wait(t, "Localizando mensagem…")
            server.disconnect()
            deadline = time.monotonic() + 7
            while (
                "Localizando mensagem…" in "\n".join(t.screen.display)
                and time.monotonic() < deadline
            ):
                t.read(0.03)
            self.assertNotIn("Localizando mensagem…", "\n".join(t.screen.display))
            # Revalidation may wait for the active REST read. Either it resets
            # the location first, or the target becomes ready before recovery.
            if "Busca interrompida." in "\n".join(t.screen.display):
                t.send("\x1b")
                self.wait(t, "Detalhes da mensagem")
            else:
                self.assert_centered_target(t, "Mensagem 0040")

    def test_search_arrows_visit_buttons_results_and_preserve_text_cursor(self):
        for width in (100, 44):
            with self.subTest(width=width), DiscoveryMock(10) as server:
                t = self.login(server)
                self.resize(t, 32 if width == 100 else 22, width)
                t.send("\x1bOR")
                self.wait(t, "Buscar mensagens no servidor")
                normal = self.button_style(t, "[Buscar]")
                # Left/Right in the text field must still move its cursor.
                t.send("Mensagem 000\x1b[D1\x1b[C\x1b[B")
                self.until(t, lambda: self.button_style(t, "[Buscar]") != normal)
                focused = self.button_style(t, "[Buscar]")
                self.assertNotEqual(focused, normal)
                self.assertEqual(self.button_style(t, "[Filtros]"), normal)
                t.send("\x1b[C")
                self.until(t, lambda: self.button_style(t, "[Filtros]") == focused)
                self.assertEqual(self.button_style(t, "[Filtros]"), focused)
                self.assertEqual(self.button_style(t, "[Buscar]"), normal)
                t.send("\x1b[D")
                self.until(t, lambda: self.button_style(t, "[Buscar]") == focused)
                self.assertEqual(self.button_style(t, "[Buscar]"), focused)
                t.send("\x1b[C\n")
                self.wait(t, "Filtros da busca")
                self.assertEqual(server.search_calls, [])
                t.send("\x1b")
                self.wait(t, "Buscar mensagens no servidor")
                t.send("\x1b[A\x1b[B\n")
                self.wait(t, "1 resultado(s)")
                self.assertEqual(server.search_calls[-1][0]["text"], "Mensagem 0010")
                t.send("\x1b[A")
                self.until(t, lambda: self.button_style(t, "[Buscar]") == focused)
                self.assertEqual(self.button_style(t, "[Buscar]"), focused)
                t.send("\x1b[C\x1b[B\n")
                self.wait(t, "Detalhes da mensagem")
                t.send("\x1b")
                self.wait(t, "1 resultado(s)")
                t.send("\x1b[A\n")
                deadline = time.monotonic() + 3
                while len(server.search_calls) < 2 and time.monotonic() < deadline:
                    t.read(0.03)
                self.assertEqual(len(server.search_calls), 2)
                self.assertNotIn("Detalhes da mensagem", "\n".join(t.screen.display))

    def test_pin_detail_arrows_select_go_and_unpin_without_activating(self):
        for width in (100, 44):
            with self.subTest(width=width), DiscoveryMock(10) as server:
                t = self.login(server)
                self.resize(t, 32 if width == 100 else 22, width)
                t.send("\x1b[19~")
                self.wait(t, "3 resultado(s)")
                t.send("0001\n")
                self.wait(t, "Detalhes da mensagem")
                self.wait(t, "[Ir para mensagem]")
                self.wait(t, "[Desafixar]")
                focused = self.button_style(t, "[Ir para mensagem]")
                normal = self.button_style(t, "[Desafixar]")
                self.assertNotEqual(focused, normal)
                t.send("\x1b[C\x1b[B")
                self.until(t, lambda: self.button_style(t, "[Desafixar]") == focused)
                self.assertEqual(self.button_style(t, "[Desafixar]"), focused)
                self.assertEqual(self.button_style(t, "[Ir para mensagem]"), normal)
                self.assertEqual(server.pin_calls, [])
                t.send("\x1b[D")
                self.until(t, lambda: self.button_style(t, "[Ir para mensagem]") == focused)
                self.assertEqual(self.button_style(t, "[Ir para mensagem]"), focused)
                self.assertEqual(self.button_style(t, "[Desafixar]"), normal)
                t.send("\n")
                self.assertNotIn("Detalhes da mensagem", "\n".join(t.screen.display))
                self.assertEqual(server.pin_calls, [])
                t.send("\x1b[19~")
                self.wait(t, "3 resultado(s)")
                t.send("0001\n")
                self.wait(t, "Detalhes da mensagem")
                t.send("\x1b[C\n")
                self.wait(t, "0 resultado(s)")
                self.assertEqual(server.pin_calls, [("DELETE", CHANNEL, uuid(101), None)])
                t.send("\t\x15")
                self.wait(t, "2 resultado(s)")

    def test_pins_filter_by_text_nickname_username_scroll_and_jump(self):
        with DiscoveryMock(205) as server:
            target = server.messages[39]
            target.update(content="Original remoto", author_id=uuid(21))
            server.pin_ids[CHANNEL] = [m["id"] for m in server.messages[:35]] + [target["id"]]
            t = self.login(server)
            self.resize(t, 32, 100)
            self.click(t, "🖈")
            self.wait(t, "36 resultado(s)")
            before = sum(r[1].split("?")[0].endswith("/messages") for r in server.requests)
            t.send("original")
            self.wait(t, "1 resultado(s)")
            self.wait(t, "Original remoto")
            t.send("\x15Apelido")
            self.wait(t, "Apelido · Original remoto")
            t.send("\x15Autor 5")
            self.wait(t, "Apelido · Original remoto")
            t.send("\x15inexistente")
            self.wait(t, "0 resultado(s)")
            t.send("\x15original\n")
            self.wait(t, "Detalhes da mensagem")
            self.wait(t, "[Desafixar]")
            self.assertEqual(
                sum(r[1].split("?")[0].endswith("/messages") for r in server.requests), before
            )
            self.assertEqual(server.marked, [])
            t.send("\x1b")
            self.wait(t, "1 resultado(s)")
            t.send("\n\n")
            self.assert_centered_target(t, "Original remoto")

    def test_pin_context_menu_unpin_detail_and_failure_keeps_modal(self):
        with DiscoveryMock(10) as server:
            t = self.login(server)
            self.resize(t, 32, 160)
            t.send("\x1b[18~m")
            self.wait(t, "Fixar mensagem")
            self.click(t, "Fixar mensagem")
            self.wait(t, "Mensagem fixada.")
            self.assertEqual(len(server.pin_calls), 1)
            t.send("\x1b[19~")
            self.wait(t, "Mensagens fixadas")
            self.wait(t, "4 resultado(s)")
            t.send("0010\n")
            self.wait(t, "Detalhes da mensagem")
            server.pin_status = 403
            self.click(t, "[Desafixar]")
            self.wait(t, "Sem permissão")
            self.wait(t, "Detalhes da mensagem")
            server.pin_status = None
            self.click(t, "[Desafixar]")
            self.wait(t, "0 resultado(s)")  # Preserve the query after removing its only match.
            t.send("\t\x15")
            self.wait(t, "3 resultado(s)")
            self.assertEqual(len(server.pin_calls), 3)

    def test_search_is_explicit_filters_draft_more_detail_and_cross_channel_jump(self):
        with DiscoveryMock(205) as server:
            target = server.messages[39]
            target["content"] = "Destino de busca"
            t = self.login(server)
            self.resize(t, 32, 100)
            self.click(t, "🔍")
            self.wait(t, "Buscar mensagens no servidor")
            t.send("Destino")
            t.read(0.25)
            self.assertEqual(server.search_calls, [])
            t.send("\n")
            self.wait(t, "1 resultado(s)")
            t.send("\n")
            self.wait(t, "Detalhes da mensagem")
            self.assertNotIn("[Desafixar]", "\n".join(t.screen.display))
            t.send("\x1b")
            self.wait(t, "1 resultado(s)")
            self.click(t, "[Filtros]")
            self.wait(t, "Filtros da busca")
            self.click(t, "Anexos: Qualquer")
            self.wait(t, "Anexos: Com anexos")
            t.send("\x1b")
            self.wait(t, "1 resultado(s)")
            self.assertEqual(len(server.search_calls), 1)
            t.send("\x1b")
            self.click(t, "outro")
            self.wait(t, "Conteúdo do outro canal")
            t.send("\x1bOR")
            self.wait(t, "Buscar mensagens no servidor")
            t.send("Destino\n")
            self.wait(t, "1 resultado(s)")
            before = sum(CHANNEL + "/messages" in r[1] for r in server.requests)
            t.send("\n")
            self.wait(t, "Detalhes da mensagem")
            self.assertEqual(sum(CHANNEL + "/messages" in r[1] for r in server.requests), before)
            t.send("\n")
            self.assert_centered_target(t, "Destino de busca")
            self.assertEqual(server.marked, [])

    def test_filter_controls_selectors_false_attachments_dates_and_cancel(self):
        with DiscoveryMock(10) as server:
            t = self.login(server)
            self.resize(t, 32, 100)
            t.send("\x1bOR")
            self.wait(t, "Buscar mensagens no servidor")
            self.click(t, "[Filtros]")
            self.click(t, "Autor: Qualquer")
            self.wait(t, "Selecionar autor")
            self.wait(t, "@bob")
            t.send("bob\n")
            self.wait(t, "Autor: Bia")
            self.click(t, "Conversa: Servidor inteiro")
            self.wait(t, "Selecionar conversa")
            t.send("geral\n")
            self.wait(t, "Conversa: geral")
            self.click(t, "Menciona: Qualquer")
            self.wait(t, "Selecionar menção")
            t.send("bob\n")
            self.wait(t, "Menciona: Bia")
            self.click(t, "Contém link")
            self.click(t, "Data inicial:")
            t.send("2026-10-12")
            self.click(t, "Data final:")
            t.send("2026-10-13")
            self.click(t, "Anexos: Qualquer")
            self.click(t, "Anexos: Com anexos")
            self.click(t, "Ordem: Mais recentes")
            self.click(t, "[Aplicar]")
            self.assertEqual(server.search_calls, [])
            self.click(t, "[Buscar]")
            self.wait(t, "0 resultado(s)")
            until = time.monotonic() + 3
            while not server.search_calls and time.monotonic() < until:
                t.read(0.03)
            self.assertEqual(
                server.search_calls[-1][0],
                dict(
                    author=uuid(2),
                    channel_id=CHANNEL,
                    mention=uuid(2),
                    has="link",
                    date_start="2026-10-12",
                    date_end="2026-10-13",
                    contains_attachment=False,
                    order="asc",
                ),
            )

    def test_pagination_dirty_state_resize_and_keyboard_focus(self):
        with DiscoveryMock(205) as server:
            t = self.login(server)
            t.send("\x1bOR")
            self.wait(t, "Buscar mensagens no servidor")
            t.send("Mensagem\n")
            self.wait(t, "100 resultado(s)")
            self.click(t, "[Carregar mais]")
            self.wait(t, "200 resultado(s)")
            self.click(t, "[Carregar mais]")
            self.wait(t, "205 resultado(s)")
            t.send("\t")
            t.send("\x15outra consulta")
            self.wait(t, "Alterações pendentes")
            self.assertEqual(len(server.search_calls), 3)
            self.resize(t, 22, 44)
            self.wait(t, "[Filtros]")
            self.click(t, "[Filtros]")
            self.wait(t, "Filtros da busca")
            t.send("\x1b[B" * 8)
            self.wait(t, "[Aplicar]")
            t.send("\n")
            self.wait(t, "[Filtros]")
            t.send("\x1b")
            t.send("\x1b[19~")
            self.wait(t, "Mensagens fixadas")
            t.send("\x1b[B\x1b[6~")
            self.wait(t, "3 resultado(s)")


if __name__ == "__main__":
    unittest.main()
