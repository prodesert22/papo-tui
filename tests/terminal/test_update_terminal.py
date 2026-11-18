"""The update check against a stand-in for the GitHub releases API, through the real TUI."""

from test_context import main as test_main

import json
import os
import tempfile
import time

from mock_server import MockServer
from settings_mock import SettingsMock
from terminal_support import Terminal
from update_mock import release, update_mock
from update_terminal import DOWN, LATEST, LIST, RIGHT, UpdateSuite

RELEASE_PAGE = "https://github.com/prodesert22/papo-tui/releases/tag/v"
SWITCH = "Verificar atualizações automaticamente"
BETA = "Aceitar versões beta"


class UpdateTerminal(UpdateSuite):
    def test_newer_release_is_announced_on_both_screens_and_detailed_in_sobre(self):
        with SettingsMock() as server, update_mock() as hub:
            hub.answer = [
                release("v99.3.0", draft=True),
                release("v99.0.0"),
                release("v1.0.0-beta.1"),
                release("v0.5.0"),
            ]
            t = Terminal(environment=hub.environment)
            self.addCleanup(t.close)
            # The sign-in screen announces it before any server is known.
            self.calls(hub, 1)
            self.wait(t, "Versão 99.0.0")
            self.wait(t, "disponível")
            self.paths(hub)
            t.send(server.url + "\tjosé\tSynthetic!123\n")
            self.wait(t, "[Canais]")
            self.wait(t, "Mensagem 0001")
            # 80 columns leave room for the short form only.
            self.wait(t, "Atualização disponível")
            t.resize(24, 140)
            self.wait(t, "Versão 99.0.0 disponível · Ctrl+S → Sobre")
            self.assertEqual(self.copies(t), [])
            # A click on the notice opens the section that has the details.
            self.click(t, "Versão 99.0.0 disponível")
            self.wait(t, "< Sobre >")
            self.wait(t, "[x] " + SWITCH)
            self.wait(t, "Versão 99.0.0 disponível. Baixe manualmente em:")
            self.wait(t, RELEASE_PAGE + "99.0.0")
            self.wait(t, "Verificar agora")
            self.dump(t, "update-available")
            # Tab enters the lines on the source link; the release page is three below,
            # after the two switches.
            t.send("\t" + DOWN * 3 + "\n")
            self.wait(t, "Cópia solicitada.")
            self.assertEqual(self.copies(t), [RELEASE_PAGE + "99.0.0"])
            t.send("\x1b")
            self.wait(t, "Mensagem 0001")
            # Once the details were shown, the footer stops repeating it.
            self.gone(t, "Ctrl+S → Sobre")
            self.assertEqual(len(hub.calls), 1, hub.calls)
            with open(self.state_file(t), encoding="utf-8") as f:
                state = json.load(f)
            self.assertEqual(state["contexts"][0]["latest"], "99.0.0")
            self.assertLess(abs(state["contexts"][0]["checked"] - time.time()), 120)

    def test_current_version_ignores_foreign_text_and_keeps_a_minute_between_requests(self):
        with SettingsMock() as server, update_mock() as hub:
            hostile = release("v99.0.0\x1b]0;hostile-title\x07")
            hostile.update(
                name="\x1b]0;hostile-title\x07", body="\x1b[2J\x1b]52;c;aG9zdGlsZQ==\x07"
            )
            older = release("v0.5.0")
            older.update(name="\x1b]0;hostile-title\x07", body="\x1b[2Jhostile")
            hub.answer = [hostile, older]
            t = self.login(server, environment=hub.environment)
            self.wait(t, "Mensagem 0001")
            self.calls(hub, 1)
            self.open_sobre(t)
            self.wait(t, "Você está na versão mais recente.")
            self.assertNotIn("disponível", self.text(t))
            self.assertNotIn(b"hostile-title", t.output)
            self.assertEqual(self.copies(t), [])
            self.dump(t, "update-current")
            # The automatic check just ran: one on request must wait.
            self.check_now(t)
            self.wait(t, "Aguarde um minuto antes de verificar de novo.")
            self.quiet(t, hub, 1, 1.5)
            self.paths(hub)

    def test_switch_off_is_remembered_and_then_only_a_request_checks(self):
        root = tempfile.TemporaryDirectory(prefix="papo-update-config-")
        self.addCleanup(root.cleanup)
        with SettingsMock() as server, update_mock() as hub:
            hub.answer = [release("v0.5.0")]
            environment = dict(
                hub.environment,
                XDG_CONFIG_HOME=root.name,
                XDG_STATE_HOME=os.path.join(root.name, "state"),
            )
            config = os.path.join(root.name, "papo-tui", "config.json")
            state = os.path.join(root.name, "state", "papo-tui", "update.json")
            t = self.login(server, environment=environment)
            self.wait(t, "Mensagem 0001")
            self.calls(hub, 1)
            self.open_sobre(t)
            self.wait(t, "[x] " + SWITCH)
            t.send("\t" + DOWN + " ")
            self.wait(t, "[ ] " + SWITCH)
            with open(config, encoding="utf-8") as f:
                saved = json.load(f)
            self.assertEqual((saved["version"], saved["update_check"]), (15, False))
            # A click toggles it too, and each change is saved at once.
            self.click(t, SWITCH)
            self.wait(t, "[x] " + SWITCH)
            with open(config, encoding="utf-8") as f:
                self.assertTrue(json.load(f)["update_check"])
            self.click(t, SWITCH)
            self.wait(t, "[ ] " + SWITCH)
            self.assertTrue(t.close())
            # More than a day later, a run with the switch off asks nothing.
            with open(state, encoding="utf-8") as f:
                stored = json.load(f)
            stored["contexts"][0]["checked"] -= 2 * 86400
            with open(state, "w", encoding="utf-8") as f:
                json.dump(stored, f)
            # The saved profile fills the server address; the form starts on the user.
            t = Terminal(environment=environment)
            self.addCleanup(t.close)
            self.wait(t, server.url)
            t.send("josé\tSynthetic!123\n")
            self.wait(t, "[Canais]")
            self.wait(t, "Mensagem 0001")
            # The automatic check would start three seconds after the program.
            self.quiet(t, hub, 1, 5)
            self.open_sobre(t)
            self.wait(t, "[ ] " + SWITCH)
            self.check_now(t)
            self.calls(hub, 2)
            self.wait(t, "Você está na versão mais recente.")
            self.paths(hub)

    def test_beta_versions_are_offered_only_after_the_switch_is_turned_on(self):
        with SettingsMock() as server, update_mock() as hub:
            hub.answer = [
                release("v99.1.0-rc.1"),
                release("v99.1.0-beta.2"),
                release("v99.0.0"),
                release("v0.5.0"),
            ]
            t = self.login(server, environment=hub.environment)
            self.wait(t, "Mensagem 0001")
            self.calls(hub, 1)
            self.open_sobre(t)
            # Off as the program comes: only final versions are asked for.
            self.wait(t, "[ ] " + BETA)
            self.wait(t, "Versão 99.0.0 disponível.")
            self.assertEqual(hub.calls, [(LATEST, None)])
            self.assertNotIn("99.1.0", self.text(t))
            # Two lines below the source link; turning it on asks again, this
            # time for the newest release of any kind.
            t.send("\t" + DOWN * 2 + " ")
            self.wait(t, "[x] " + BETA)
            self.calls(hub, 2)
            self.assertEqual(hub.calls[1], (LIST, None))
            self.wait(t, "Versão 99.1.0-rc.1 disponível.")
            self.wait(t, RELEASE_PAGE + "99.1.0-rc.1")
            self.dump(t, "update-beta")
            with open(os.path.join(t.configuration.name, "papo-tui", "config.json")) as f:
                saved = json.load(f)
            self.assertEqual((saved["version"], saved["update_prereleases"]), (15, True))
            with open(self.state_file(t), encoding="utf-8") as f:
                stored = json.load(f)["contexts"][0]
            self.assertEqual((stored["latest"], stored["prereleases"]), ("99.1.0-rc.1", True))
            # Off again: the pre-release is forgotten and the final one is back.
            self.click(t, BETA)
            self.wait(t, "[ ] " + BETA)
            self.calls(hub, 3)
            self.assertEqual(hub.calls[2], (LATEST, None))
            self.wait(t, "Versão 99.0.0 disponível.")
            self.assertNotIn("99.1.0", self.text(t))
            with open(os.path.join(t.configuration.name, "papo-tui", "config.json")) as f:
                self.assertFalse(json.load(f)["update_prereleases"])

    def test_beta_switch_is_remembered_and_an_answer_for_the_other_choice_is_not_used(self):
        root = tempfile.TemporaryDirectory(prefix="papo-update-beta-")
        self.addCleanup(root.cleanup)
        with SettingsMock() as server, update_mock() as hub:
            hub.answer = [release("v99.1.0-beta.1"), release("v99.0.0")]
            environment = dict(
                hub.environment,
                XDG_CONFIG_HOME=root.name,
                XDG_STATE_HOME=os.path.join(root.name, "state"),
            )
            t = self.login(server, environment=environment)
            self.wait(t, "Mensagem 0001")
            self.calls(hub, 1)
            self.open_sobre(t)
            self.wait(t, "Versão 99.0.0 disponível.")
            t.send("\t" + DOWN * 2 + " ")
            self.wait(t, "Versão 99.1.0-beta.1 disponível.")
            self.calls(hub, 2)
            self.assertTrue(t.close())
            # The next run starts with the switch on and the stored answer,
            # fresh and for this choice: nothing is asked again.
            t = Terminal(environment=environment)
            self.addCleanup(t.close)
            self.wait(t, server.url)
            self.wait(t, "Versão 99.1.0-beta.1")
            t.send("josé\tSynthetic!123\n")
            self.wait(t, "[Canais]")
            self.wait(t, "Mensagem 0001")
            self.quiet(t, hub, 2, 4)
            self.open_sobre(t)
            self.wait(t, "[x] " + BETA)
            self.wait(t, "Versão 99.1.0-beta.1 disponível.")
            self.assertTrue(t.close())
            # Turned off by hand in the file: the stored answer was for the
            # other choice, so the program asks again instead of offering it.
            config = os.path.join(root.name, "papo-tui", "config.json")
            with open(config, encoding="utf-8") as f:
                saved = json.load(f)
            saved["update_prereleases"] = False
            with open(config, "w", encoding="utf-8") as f:
                json.dump(saved, f)
            t = Terminal(environment=environment)
            self.addCleanup(t.close)
            self.wait(t, server.url)
            self.calls(hub, 3)
            self.assertEqual(hub.calls[2], (LATEST, None))
            self.wait(t, "Versão 99.0.0")
            self.assertNotIn("99.1.0", self.text(t))

    def test_environment_switch_keeps_the_run_from_checking_by_itself(self):
        with SettingsMock() as server, update_mock() as hub:
            hub.answer = [release("v99.0.0")]
            t = self.login(server, environment=dict(hub.environment, PAPO_UPDATE_CHECK="0"))
            self.wait(t, "Mensagem 0001")
            # The automatic check would start three seconds after the program.
            self.quiet(t, hub, 0, 5)
            self.open_sobre(t)
            # The saved switch is untouched; the variable only holds this run.
            self.wait(t, "[x] " + SWITCH)
            self.wait(t, "PAPO_UPDATE_CHECK=0: as atualizações só são verificadas a pedido.")
            self.check_now(t)
            self.calls(hub, 1)
            self.wait(t, "Versão 99.0.0 disponível. Baixe manualmente em:")
            self.paths(hub)

    def test_github_limit_is_reported_once_and_never_slows_the_chat(self):
        with SettingsMock() as server, update_mock() as hub:
            hub.answer = (429, None, None, "3600")
            t = self.login(server, environment=hub.environment)
            self.wait(t, "Mensagem 0001")
            self.calls(hub, 1)
            self.open_sobre(t)
            self.wait(t, "O GitHub limitou as consultas deste endereço. Tente mais tarde.")
            self.dump(t, "update-limited")
            t.send("\x1b")
            self.wait(t, "Mensagem 0001")
            # The hour GitHub asked for is the update check's alone: a request
            # to the chat server still goes out at once.
            before = len(server.settings_calls)
            t.send("\x13")
            self.wait(t, "Configurações")
            t.send(RIGHT * 2 + "\t" + DOWN * 3 + " \t\n")
            self.wait(t, "Configurações da conta salvas")
            self.assertEqual(len(server.settings_calls), before + 1)
            # A refusal is not retried.
            self.quiet(t, hub, 1, 4)
            self.paths(hub)

    def test_unexpected_answer_is_retried_twice_and_then_left_for_the_next_day(self):
        with SettingsMock() as server, update_mock() as hub:
            hub.answer = (200, {"message": "not a release"})
            t = self.login(server, environment=hub.environment)
            self.wait(t, "Mensagem 0001")
            # Three attempts, about one and two seconds apart, then no more.
            self.calls(hub, 3, seconds=14)
            self.open_sobre(t)
            self.wait(t, "O GitHub respondeu algo inesperado. Tente mais tarde.")
            self.quiet(t, hub, 3, 5)
            self.assertFalse(os.path.exists(self.state_file(t)))
            self.paths(hub)

    def test_untrusted_certificate_plain_http_and_no_connection_send_nothing(self):
        with SettingsMock() as server, update_mock() as hub, update_mock() as other:
            hub.answer = [release("v99.0.0")]
            # Another authority signed the certificate the client is told to trust.
            environment = dict(hub.environment, CURL_CA_BUNDLE=other.environment["CURL_CA_BUNDLE"])
            t = self.login(server, environment=environment)
            self.wait(t, "Mensagem 0001")
            self.open_sobre(t)
            self.wait(t, "O certificado do GitHub não é confiável; nada foi consultado.")
            self.assertEqual(hub.calls, [])
            self.assertNotIn("99.0.0", self.text(t))
            self.assertTrue(t.close())
            # The endpoint is never asked without TLS.
            with MockServer() as plain:
                plain.api_handler = lambda method, path, body: (200, [release("v99.0.0")])
                t = self.login(
                    server,
                    environment={
                        "PAPO_UPDATE_API_URL": plain.url + "/github",
                        "PAPO_UPDATE_CHECK": "1",
                    },
                )
                self.wait(t, "Mensagem 0001")
                self.open_sobre(t)
                self.wait(t, "O GitHub respondeu algo inesperado. Tente mais tarde.")
                self.assertEqual([r for r in plain.requests if "/github/" in r[1]], [])
                self.assertNotIn("99.0.0", self.text(t))
                self.assertTrue(t.close())
            # Nothing listens there: the failure names the connection.
            t = self.login(
                server,
                environment={
                    "PAPO_UPDATE_API_URL": "https://127.0.0.1:9/x",
                    "PAPO_UPDATE_CHECK": "1",
                },
            )
            self.wait(t, "Mensagem 0001")
            self.open_sobre(t)
            self.wait(t, "Não foi possível consultar o GitHub. Verifique a conexão.")


if __name__ == "__main__":
    test_main()
