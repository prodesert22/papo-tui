"""Settings (Ctrl+S → Aparência / Notificações / Sobre) and sounds through the real TUI."""

from test_context import main as test_main

import base64
import copy
import os
import pathlib
import re
import subprocess
import tempfile
import time

from r8_mock import AUTHOR, uuid
from settings_mock import DEFAULT, SettingsMock, settings
import terminal_discovery as support
import terminal_support

DOWN, UP, LEFT, RIGHT = "\x1bOB", "\x1bOA", "\x1bOD", "\x1bOC"
REPOSITORY = "https://github.com/prodesert22/papo-tui"
CLOCK = re.compile(r"\d\d:\d\d:\d\d")


def expected(**changes):
    value = copy.deepcopy(DEFAULT)
    for section, update in changes.items():
        if isinstance(update, dict):
            value[section].update(update)
        else:
            value[section] = update
    return {"config": value}


class UserSettingsTerminal(support.R8Terminal):
    def history(self, t):
        return "\n".join(line for line in t.screen.display if "Mensagem 00" in line or "·" in line)

    def calls(self, server, count, seconds=6):
        end = time.monotonic() + seconds
        while len(server.settings_calls) < count and time.monotonic() < end:
            time.sleep(0.03)
        self.assertEqual(len(server.settings_calls), count, server.settings_calls)
        return server.settings_calls

    def open_tab(self, t, rights):
        t.send("\x13")
        self.wait(t, "Configurações")
        t.send(RIGHT * rights + "\t")

    def test_whoami_settings_shape_the_history_and_aparencia_saves_them_whole(self):
        with SettingsMock() as server:
            server.user_settings = settings(display={"showTimestamps": False})
            # Kitty keeps its indexed palette, so the screen shows the theme's
            # fixed xterm colors instead of redefined ones.
            t = self.login(server, environment={"KITTY_WINDOW_ID": "settings-fixture"})
            self.wait(t, "Mensagem 0001")
            # Read from whoami: no time in the message headers.
            self.assertIsNone(CLOCK.search(self.history(t)), self.history(t))
            self.open_tab(t, 1)
            self.wait(t, "[ ] Mostrar horários")
            self.wait(t, "[x] Mostrar avatares")
            self.wait(t, "Tema: < Sistema >")
            # A click on the left half steps back, on the right half forward;
            # switches toggle.
            self.click(t, "Mostrar horários")
            self.wait(t, "[x] Mostrar horários")
            self.click(t, "Mostrar avatares")
            self.wait(t, "[ ] Mostrar avatares")
            self.click(t, "Tema:")
            self.wait(t, "Tema: < Claro >")
            # The selected item says what it does.
            self.wait(t, "Cores da tela.")
            self.dump(t, "appearance")
            self.click(t, "Densidade:")
            self.wait(t, "Densidade: < Compacta >")
            # Keys work on the focused item too.
            t.send(RIGHT * 2)
            self.wait(t, "Densidade: < Confortável >")
            self.assertEqual(server.settings_calls, [])
            t.send("\t\n")
            self.wait(t, "Configurações da conta salvas")
            # The whole config is sent, as the server replaces it.
            self.assertEqual(
                self.calls(server, 1),
                [
                    expected(
                        theme="light",
                        display={"messageDensity": "comfortable", "showAvatars": False},
                    )
                ],
            )
            t.send("\x1b")
            end = time.monotonic() + 5
            while not CLOCK.search(self.history(t)) and time.monotonic() < end:
                t.read(0.05)
            self.assertIsNotNone(CLOCK.search(self.history(t)), "\n".join(t.screen.display))
            # Claro: the conversation is drawn on a light gray background now.
            self.assertTrue(self.light_text_background(t, "Mensagem 0001"), self.colors(t))
            t.read(0.5)
            self.assertEqual(len(server.settings_calls), 1)

    def colors(self, t):
        return {
            t.screen.buffer[y][x].bg
            for y, line in enumerate(t.screen.display)
            if "Mensagem 0001" in line
            for x in range(line.index("Mensagem 0001"), line.index("Mensagem 0001") + 8)
        }

    def light_text_background(self, t, text):
        end = time.monotonic() + 5
        while time.monotonic() < end:
            backgrounds = self.colors(t)
            if backgrounds and all(
                re.fullmatch(r"[0-9a-f]{6}", bg)
                and sum(int(bg[i : i + 2], 16) for i in (0, 2, 4)) > 3 * 150
                for bg in backgrounds
            ):
                return True
            t.read(0.05)
        return False

    def dump(self, t, name):
        """Keeps the screen text as evidence when PAPO_SCREEN_DUMPS names a folder."""
        folder = os.environ.get("PAPO_SCREEN_DUMPS")
        if folder:
            with open(os.path.join(folder, name + ".txt"), "w", encoding="utf-8") as f:
                f.write("\n".join(t.screen.display) + "\n")

    def click(self, t, label):
        for y, line in enumerate(t.screen.display):
            if label in line:
                t.click(y, line.index(label) + 1)
                return
        self.fail("Item absent: " + label)

    def test_notificacoes_refusals_and_unknown_results_are_never_repeated(self):
        cases = (
            (400, "O servidor recusou as configurações"),
            (429, "Muitas alterações; aguarde e tente de novo"),
            ("drop", "Resultado não confirmado"),
            ("other-account", "Resultado não confirmado"),
        )
        for status, notice in cases:
            with self.subTest(status=status), SettingsMock() as server:
                if status == "other-account":
                    # A 200 for another account confirms nothing.
                    server.settings_reply = dict(
                        user_id=uuid(99), version=1, config=DEFAULT, updated_at="x"
                    )
                else:
                    server.settings_status = status
                t = self.login(server)
                self.open_tab(t, 2)
                self.wait(t, "[x] Sons")
                # Each switch explains itself while selected.
                self.wait(t, "Mostra avisos do sistema e toca sons")
                t.send(DOWN * 2 + " ")
                self.wait(t, "[ ] Sons")
                self.wait(t, "Toca um som quando chega uma notificação")
                if status == 400:
                    self.dump(t, "notifications")
                t.send("\t\n")
                self.wait(t, notice)
                self.assertEqual(self.calls(server, 1), [expected(notifications={"sound": False})])
                # The server's text never brings terminal controls with it.
                self.assertNotIn(b"\x1b]52", t.output)
                t.read(0.6)
                self.assertEqual(len(server.settings_calls), 1)
                # The draft stays for a manual retry, which is a new request.
                self.assertIn("[ ] Sons", "\n".join(t.screen.display))
                server.settings_status = server.settings_reply = None
                t.send("\n")
                self.wait(t, "Configurações da conta salvas")
                self.assertEqual(len(self.calls(server, 2)), 2)

    def sounds(self, log):
        try:
            with open(log, encoding="utf-8") as f:
                return f.read().splitlines()
        except FileNotFoundError:
            return []

    def wait_sounds(self, log, count, seconds=6):
        end = time.monotonic() + seconds
        while len(self.sounds(log)) < count and time.monotonic() < end:
            time.sleep(0.05)
        return self.sounds(log)

    def notify(self, server, n, content):
        server.emit(
            dict(
                type="new_notification",
                id=uuid(9100 + n),
                user_id=AUTHOR,
                message_id=uuid(9200 + n),
                message_content=content,
            )
        )

    def test_alert_sounds_follow_the_account_and_this_device(self):
        root = tempfile.TemporaryDirectory(prefix="papo-sounds-")
        self.addCleanup(root.cleanup)
        log = os.path.join(root.name, "sounds.log")
        mention = "Olá @mention(<@" + uuid(1) + ">)"
        with SettingsMock() as server:
            t = self.login(server, environment={"PAPO_SOUND_LOG": log})
            self.wait(t, "Tempo real conectado")
            self.notify(server, 1, mention)
            self.assertEqual(self.wait_sounds(log, 1), ["sound=mention"])
            self.notify(server, 2, "Mensagem comum")
            self.assertEqual(self.wait_sounds(log, 2), ["sound=mention", "sound=message"])
            # Ctrl+D → Preferências: this device's switch silences everything.
            t.send("\x04")
            self.wait(t, "Notificações")
            t.send("\t\n")
            self.wait(t, "[x] Sons neste aparelho")
            t.send(DOWN * 2 + " ")
            self.wait(t, "[ ] Sons neste aparelho")
            self.wait(t, "Sons neste aparelho: toca os sons")
            self.dump(t, "device")
            t.send("\x1b\x1b")
            self.notify(server, 3, mention)
            time.sleep(3)
            self.assertEqual(len(self.sounds(log)), 2)
            t.send("\x04")
            self.wait(t, "Notificações")
            t.send("\t\n")
            t.send(DOWN * 2 + " ")
            self.wait(t, "[x] Sons neste aparelho")
            t.send("\x1b\x1b")
            # The account: without mention alerts, only ordinary ones sound.
            self.open_tab(t, 2)
            t.send(DOWN * 3 + " ")
            self.wait(t, "[ ] Avisar quando mencionarem você")
            t.send("\t\n")
            self.wait(t, "Configurações da conta salvas")
            t.send("\x1b")
            self.notify(server, 4, mention)
            time.sleep(3)
            self.assertEqual(len(self.sounds(log)), 2)
            self.notify(server, 5, "Outra mensagem")
            self.assertEqual(self.wait_sounds(log, 3)[2:], ["sound=message"])

    def copies(self, t):
        return [
            base64.b64decode(value).decode()
            for value in re.findall(rb"\x1b\]52;c;([A-Za-z0-9+/=]*)\x07", t.output)
        ]

    def test_sobre_shows_the_program_facts_and_copies_the_source_link(self):
        version = subprocess.run(
            [terminal_support.BINARY, "--version"], capture_output=True, text=True, check=True
        ).stdout.split()[1]
        # The holder shown is the one in the LICENSE file shipped with the program.
        license_lines = (
            (pathlib.Path(__file__).resolve().parents[2] / "LICENSE")
            .read_text(encoding="utf-8")
            .splitlines()
        )
        self.assertEqual(license_lines[0], "MIT License")
        with SettingsMock() as server:
            t = self.login(server)
            self.wait(t, "Mensagem 0001")
            # Sobre is the last section: Left from Perfil wraps to it.
            t.send("\x13")
            self.wait(t, "Configurações")
            t.send(LEFT)
            self.wait(t, "< Sobre >")
            for text in (
                "Papo TUI " + version,
                "Cliente de conversas para terminal",
                "Licença: MIT · " + license_lines[2],
                "Autor: Papo TUI contributors",
                "Código-fonte: " + REPOSITORY,
                "Licenças de terceiros:",
                "Copiar link",
                "Fechar",
            ):
                self.wait(t, text)
            self.assertEqual(self.copies(t), [])
            # Tab enters the lines on the source link; Enter copies it.
            t.send("\t\n")
            self.wait(t, "Cópia solicitada.")
            self.assertEqual(self.copies(t), [REPOSITORY])
            # Leaving the section forgets the notice; a click on the link copies too.
            t.send(UP + RIGHT)
            self.wait(t, "< Perfil >")
            t.send(LEFT)
            self.wait(t, "< Sobre >")
            self.assertNotIn("Cópia solicitada.", "\n".join(t.screen.display))
            self.click(t, "Código-fonte:")
            self.wait(t, "Cópia solicitada.")
            self.assertEqual(self.copies(t), [REPOSITORY] * 2)
            # So does the section's button; the update switches live elsewhere.
            t.send(UP + LEFT + RIGHT + "\t\t\n")
            self.wait(t, "Cópia solicitada.")
            self.assertEqual(self.copies(t), [REPOSITORY] * 3)
            self.assertNotIn("Verificar", "\n".join(t.screen.display))
            self.dump(t, "sobre")
            # A short window keeps the version, the license, the author and the link.
            t.resize(14, 60)
            self.wait(t, "< Sobre >")
            self.wait(t, "Código-fonte: " + REPOSITORY)
            self.wait(t, "Papo TUI " + version)
            self.wait(t, "Autor: Papo TUI contributors")
            for dropped in ("Cliente de conversas", "Licenças de terceiros"):
                self.assertNotIn(dropped, "\n".join(t.screen.display))
            t.resize(24, 80)
            self.wait(t, "Cliente de conversas para terminal")
            t.send("\x1b")
            self.wait(t, "Mensagem 0001")
            self.assertNotIn("Papo TUI " + version, "\n".join(t.screen.display))

    def test_atualizacoes_holds_the_update_switches_between_atalhos_and_sistema(self):
        with SettingsMock() as server:
            t = self.login(server)
            self.wait(t, "Mensagem 0001")
            t.send("\x13")
            self.wait(t, "Configurações")
            # The seventh section: six steps from Perfil.
            t.send(RIGHT * 6)
            self.wait(t, "< Atualizações >")
            for text in (
                "[x] Verificar atualizações automaticamente",
                "[ ] Aceitar versões beta",
                # The test terminals keep the program from asking by itself.
                "PAPO_UPDATE_CHECK=0: as atualizações só são verificadas a pedido.",
                "Verificar agora",
                "Fechar",
            ):
                self.wait(t, text)
            # The program's facts are in Sobre, not here.
            self.assertNotIn("Licença:", "\n".join(t.screen.display))
            self.assertNotIn("Código-fonte:", "\n".join(t.screen.display))
            self.dump(t, "atualizacoes")
            t.send(RIGHT)
            self.wait(t, "< Sistema >")
            t.send(LEFT * 2)
            self.wait(t, "< Atalhos >")
            # A short window keeps both switches and the state.
            t.send(RIGHT)
            self.wait(t, "< Atualizações >")
            t.resize(14, 60)
            self.wait(t, "< Atualizações >")
            self.wait(t, "[x] Verificar atualizações automaticamente")
            self.wait(t, "[ ] Aceitar versões beta")
            self.wait(t, "PAPO_UPDATE_CHECK=0")


if __name__ == "__main__":
    test_main()
